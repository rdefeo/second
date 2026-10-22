#include "audio.h"

#include <furi.h>
#include <furi_hal.h>
#include <furi_hal_bus.h>
#include <furi_hal_interrupt.h>
#include <furi_hal_speaker.h>
#include <stm32wbxx_ll_dma.h>
#include <stm32wbxx_ll_dmamux.h>
#include <stm32wbxx_ll_tim.h>
#include <storage/storage.h>
#include <stdio.h>

#define TAG "Audio"

/* The stream ring is two halves of HALF samples (64 ms each). Every half
 * that finishes playing is counted by the interrupt; "slot" n is the n-th
 * half ever played, and slot n uses ring half (n & 1). The refill thread
 * fills the next slot from the stream at the position that slot belongs
 * to, so a late refill costs a glitch, never a lasting delay.
 *
 * The 16 kHz stream is played at twice the rate, with a sample halfway
 * between each pair (linear interpolation): smaller steps, so less of the
 * harsh mirror images of the treble. The speaker PWM has PWM_LEVELS steps
 * (its carrier 64 MHz / 1000 = 64 kHz, two periods per output sample), so
 * the interpolated values and the volume keep their detail. That's done
 * by the DMA interrupt into a small buffer of PWM values (DMA_N per half,
 * 2 ms), so the 16-bit output costs little RAM: the big scenes need it. */
#define HALF       1024
#define UP         2 /* output samples per stream sample */
#define DMA_N      64 /* output samples per DMA buffer half */
#define PWM_LEVELS 1000
#define PWM_MID    (PWM_LEVELS / 2)
#define READ       512 /* SD reads stay short: the card shares the LCD's bus */
#define DMA_CH     LL_DMA_CHANNEL_1
#define SILENCE    128 /* in the stream */
#define FADE       80 /* 5 ms: crossfades and pause fades, so nothing clicks */

typedef enum {
    FlagRefill = 1 << 0,
    FlagQuit = 1 << 1,
    FlagCommand = 1 << 2,
    FlagReady = 1 << 3, /* sent to the starting thread */
} Flags;

struct Audio {
    uint8_t ring[HALF * 2]; /* the stream */
    uint16_t dma[DMA_N * 2]; /* PWM compare values, made by the interrupt */
    FuriThread* thread;
    FuriMutex* mutex;
    Storage* storage;
    File* file;
    bool tim2_was_on;
    bool ok; /* speaker acquired and running */
    FuriThreadId caller;

    volatile uint32_t played; /* slots finished, counted by the ISR */
    volatile uint32_t rd; /* the ISR's read position in the ring */
    int isr_gain; /* volume the ISR applies (ramps to `volume`) */
    int prev; /* the ISR's last output level, to interpolate from */
    uint32_t next_slot; /* next slot to fill */

    /* current stream: slot start_slot plays sample `origin` of the file */
    bool file_open;
    uint32_t file_size, file_pos;
    uint32_t loop; /* sample to go on from at the end, or AUDIO_NO_LOOP */
    uint32_t start_slot;
    int64_t origin;

    /* requests from the app, applied by the refill thread */
    char want_name[16];
    uint32_t want_offset, want_loop;
    bool want_change;
    volatile int volume;
    volatile bool paused;

    /* declicking, all in the refill thread */
    uint8_t tail[FADE]; /* the old stream's next samples after a switch... */
    bool have_tail;
    uint32_t tail_slot; /* ...crossfaded into the start of this slot */
    int fade; /* 0..FADE: pause fade-out/resume fade-in position */
    uint32_t late; /* slots that had to be skipped */
    uint32_t fill_max_ms; /* slowest slot refill since last asked */
};

/* Fill one DMA half from the ring: volume (ramping one step per sample
 * instead of jumping), scaled straight to PWM levels so a low volume keeps
 * its detail, and a sample halfway between each pair. */
static void render_dma(Audio* a, uint16_t* out) {
    int target = a->volume, g = a->isr_gain, prev = a->prev;
    uint32_t rd = a->rd;
    for(int i = 0; i < DMA_N / UP; i++) {
        if(g < target)
            g++;
        else if(g > target)
            g--;
        int cur = PWM_MID + (a->ring[rd] - SILENCE) * g * PWM_MID / (SILENCE * 256);
        out[i * UP] = (uint16_t)((prev + cur) / 2);
        out[i * UP + 1] = (uint16_t)cur;
        prev = cur;
        if(++rd == HALF * 2) rd = 0;
        if(rd % HALF == 0) { /* a slot finished: refill it */
            a->played++;
            furi_thread_flags_set(furi_thread_get_id(a->thread), FlagRefill);
        }
    }
    a->isr_gain = g;
    a->prev = prev;
    a->rd = rd;
}

static void dma_isr(void* context) {
    Audio* a = context;
    if(LL_DMA_IsActiveFlag_HT1(DMA1)) {
        LL_DMA_ClearFlag_HT1(DMA1);
        render_dma(a, a->dma);
    }
    if(LL_DMA_IsActiveFlag_TC1(DMA1)) {
        LL_DMA_ClearFlag_TC1(DMA1);
        render_dma(a, a->dma + DMA_N);
    }
}

/* Read up to n samples of the current stream for `slot`, from `skip`
 * samples into it, wrapping at the end if it loops. Returns how many. */
static size_t read_stream(Audio* a, uint32_t slot, uint32_t skip, uint8_t* dst, size_t n) {
    if(!a->file_open || slot < a->start_slot) return 0;
    int64_t pos = a->origin + (int64_t)(slot - a->start_slot) * HALF + skip;
    bool loops = a->loop < a->file_size;
    if(loops && pos >= a->file_size) pos = a->loop + (pos - a->loop) % (a->file_size - a->loop);
    if(pos < 0 || pos >= a->file_size) return 0;
    if((uint32_t)pos != a->file_pos) {
        storage_file_seek(a->file, (uint32_t)pos, true);
        a->file_pos = (uint32_t)pos;
    }
    size_t got = 0;
    while(got < n) {
        size_t want = n - got < READ ? n - got : READ;
        size_t r = storage_file_read(a->file, dst + got, want);
        if(!r) { /* end of the file */
            if(!loops) break;
            storage_file_seek(a->file, a->loop, true);
            a->file_pos = a->loop;
            continue;
        }
        got += r;
        a->file_pos += r;
    }
    return got;
}

static void fill_slot(Audio* a, uint32_t slot) {
    uint8_t* dst = a->ring + (slot & 1) * HALF;
    bool paused = a->paused;
    /* while paused, read only as long as the fade-out needs */
    size_t got = !paused || a->fade ? read_stream(a, slot, 0, dst, HALF) : 0;
    for(size_t i = got; i < HALF; i++)
        dst[i] = SILENCE;

    /* the stream changed here: crossfade from where the old one was going */
    if(a->have_tail && slot >= a->tail_slot) {
        if(slot == a->tail_slot)
            for(int i = 0; i < FADE; i++)
                dst[i] = (uint8_t)((a->tail[i] * (FADE - i) + dst[i] * i) / FADE);
        a->have_tail = false;
    }

    /* pausing fades out / resuming fades in over FADE samples */
    int target = paused ? 0 : FADE, fade = a->fade;
    if(fade != target || fade != FADE) {
        for(int i = 0; i < HALF; i++) {
            if(fade < target)
                fade++;
            else if(fade > target)
                fade--;
            dst[i] = (uint8_t)(SILENCE + (dst[i] - SILENCE) * fade / FADE);
        }
    }
    a->fade = fade;
}

/* Stream samples until the given slot starts playing. */
static uint32_t samples_until(Audio* a, uint32_t slot) {
    uint32_t played = a->played;
    uint32_t into = a->rd % HALF; /* read into the slot playing */
    if(slot <= played) return 0;
    /* what the ISR has read is still queued in the DMA buffer: up to DMA_N */
    return (slot - played - 1) * HALF + (HALF - into) + DMA_N / UP;
}

static void apply_request(Audio* a) {
    furi_mutex_acquire(a->mutex, FuriWaitForever);
    bool change = a->want_change;
    a->want_change = false;
    char name[sizeof(a->want_name)];
    memcpy(name, a->want_name, sizeof(name));
    uint32_t offset_ms = a->want_offset;
    uint32_t loop = a->want_loop;
    furi_mutex_release(a->mutex);
    if(!change) return;

    /* the new stream starts with the next unfilled slot: keep what the old
     * one would have played there, to crossfade from */
    uint32_t next = a->played + 1;
    uint32_t start = a->next_slot > next ? a->next_slot : next;
    size_t got = a->fade ? read_stream(a, start, 0, a->tail, FADE) : 0;
    memset(a->tail + got, SILENCE, FADE - got);
    a->have_tail = true;
    a->tail_slot = start;

    if(a->file_open) storage_file_close(a->file);
    a->file_open = false;
    if(!name[0]) return;
    /* packed in the .fap in release builds (unpacked to apps_assets), else
     * uploaded by tools/upload_music.sh */
    char path[64];
    snprintf(path, sizeof(path), STORAGE_APP_ASSETS_PATH_PREFIX "/music/%s", name);
    if(!storage_file_open(a->file, path, FSAM_READ, FSOM_OPEN_EXISTING)) {
        snprintf(path, sizeof(path), EXT_PATH("apps_data/second/%s"), name);
        if(!storage_file_open(a->file, path, FSAM_READ, FSOM_OPEN_EXISTING)) {
            FURI_LOG_E(TAG, "no music %s (release build, or tools/upload_music.sh)", name);
            return;
        }
    }
    a->file_open = true;
    a->file_size = (uint32_t)storage_file_size(a->file);
    a->file_pos = 0;
    a->loop = loop;
    /* the requested offset is "now": skip ahead by however long until the
     * start slot plays */
    a->start_slot = start;
    a->origin = (int64_t)offset_ms * AUDIO_RATE / 1000 + samples_until(a, a->start_slot);
}

static void refill(Audio* a) {
    uint32_t played = a->played;
    if(a->next_slot <= played) { /* fell behind: skip what already played */
        a->late += played + 1 - a->next_slot;
        a->next_slot = played + 1;
    }
    while(a->next_slot <= played + 1) {
        uint32_t t0 = furi_get_tick();
        fill_slot(a, a->next_slot);
        uint32_t dt = furi_get_tick() - t0;
        if(dt > a->fill_max_ms) a->fill_max_ms = dt;
        a->next_slot++;
    }
}

static void hw_start(Audio* a);
static void hw_stop(Audio* a);

/* The audio thread owns the speaker for its whole life. (Owning it from
 * the drawing thread would keep a mutex held there, and FreeRTOS would
 * then never drop priority it inherits from the display thread: the
 * drawing would run above the SD card service and starve the audio.) */
static int32_t audio_thread(void* context) {
    Audio* a = context;
    a->ok = furi_hal_speaker_acquire(1000);
    if(a->ok) {
        /* let the HAL set up the speaker pin and timer, then take TIM16 */
        furi_hal_speaker_start(1000, 0);
        hw_start(a);
    } else {
        FURI_LOG_E(TAG, "speaker busy");
    }
    furi_thread_flags_set(a->caller, FlagReady);
    if(!a->ok) return 0;

    for(;;) {
        uint32_t flags =
            furi_thread_flags_wait(FlagRefill | FlagQuit | FlagCommand, FuriFlagWaitAny, FuriWaitForever);
        if(flags & FuriFlagError) continue;
        if(flags & FlagQuit) break;
        if(flags & FlagCommand) apply_request(a);
        refill(a);
    }
    hw_stop(a);
    furi_hal_speaker_stop();
    furi_hal_speaker_release();
    return 0;
}

/* Move the speaker between off (0) and silence (PWM_MID) over 5 ms: a
 * jump between them clicks. */
static void ramp_level(int from, int to) {
    int step = from < to ? 1 : -1;
    for(int v = from; v != to; v += step) {
        LL_TIM_OC_SetCompareCH1(TIM16, (uint32_t)v);
        furi_delay_us(5000 / PWM_MID);
    }
    LL_TIM_OC_SetCompareCH1(TIM16, (uint32_t)to);
}

static void hw_start(Audio* a) {
    /* TIM16 (the speaker) as PWM with PWM_LEVELS steps, 64 kHz */
    LL_TIM_DisableCounter(TIM16);
    LL_TIM_SetPrescaler(TIM16, 0);
    LL_TIM_SetAutoReload(TIM16, PWM_LEVELS - 1);
    LL_TIM_OC_SetMode(TIM16, LL_TIM_CHANNEL_CH1, LL_TIM_OCMODE_PWM1);
    LL_TIM_OC_EnablePreload(TIM16, LL_TIM_CHANNEL_CH1);
    LL_TIM_OC_SetCompareCH1(TIM16, 0);
    LL_TIM_CC_EnableChannel(TIM16, LL_TIM_CHANNEL_CH1);
    LL_TIM_EnableAllOutputs(TIM16);
    LL_TIM_EnableCounter(TIM16);
    ramp_level(0, PWM_MID);

    /* TIM2 update at the output rate requests one DMA transfer each */
    a->tim2_was_on = furi_hal_bus_is_enabled(FuriHalBusTIM2);
    if(!a->tim2_was_on) furi_hal_bus_enable(FuriHalBusTIM2);
    LL_TIM_DisableCounter(TIM2);
    LL_TIM_SetPrescaler(TIM2, 0);
    LL_TIM_SetCounterMode(TIM2, LL_TIM_COUNTERMODE_UP);
    LL_TIM_SetAutoReload(TIM2, SystemCoreClock / (AUDIO_RATE * UP) - 1);
    LL_TIM_EnableDMAReq_UPDATE(TIM2);

    /* DMA1 ch1: circular, buffer (halfwords) -> TIM16->CCR1 */
    LL_DMA_DisableChannel(DMA1, DMA_CH);
    LL_DMA_ConfigTransfer(
        DMA1,
        DMA_CH,
        LL_DMA_DIRECTION_MEMORY_TO_PERIPH | LL_DMA_MODE_CIRCULAR | LL_DMA_PERIPH_NOINCREMENT |
            LL_DMA_MEMORY_INCREMENT | LL_DMA_PDATAALIGN_HALFWORD | LL_DMA_MDATAALIGN_HALFWORD |
            LL_DMA_PRIORITY_VERYHIGH);
    LL_DMA_SetPeriphRequest(DMA1, DMA_CH, LL_DMAMUX_REQ_TIM2_UP);
    LL_DMA_ConfigAddresses(
        DMA1, DMA_CH, (uint32_t)a->dma, (uint32_t) & (TIM16->CCR1), LL_DMA_DIRECTION_MEMORY_TO_PERIPH);
    LL_DMA_SetDataLength(DMA1, DMA_CH, DMA_N * 2);
    furi_hal_interrupt_set_isr(FuriHalInterruptIdDma1Ch1, dma_isr, a);
    LL_DMA_EnableIT_HT(DMA1, DMA_CH);
    LL_DMA_EnableIT_TC(DMA1, DMA_CH);
    LL_DMA_EnableChannel(DMA1, DMA_CH);
    LL_TIM_EnableCounter(TIM2);
}

static void hw_stop(Audio* a) {
    LL_TIM_DisableCounter(TIM2);
    LL_TIM_DisableDMAReq_UPDATE(TIM2);
    LL_DMA_DisableIT_HT(DMA1, DMA_CH);
    LL_DMA_DisableIT_TC(DMA1, DMA_CH);
    LL_DMA_DisableChannel(DMA1, DMA_CH);
    furi_hal_interrupt_set_isr(FuriHalInterruptIdDma1Ch1, NULL, NULL);
    if(!a->tim2_was_on) furi_hal_bus_disable(FuriHalBusTIM2);
    ramp_level((int)LL_TIM_OC_GetCompareCH1(TIM16), 0);
}

Audio* audio_start(void) {
    if(furi_hal_rtc_is_flag_set(FuriHalRtcFlagStealthMode)) {
        FURI_LOG_I(TAG, "stealth mode: no sound");
        return NULL;
    }
    Audio* a = malloc(sizeof(Audio));
    memset(a, 0, sizeof(Audio));
    memset(a->ring, SILENCE, sizeof(a->ring));
    for(size_t i = 0; i < DMA_N * 2; i++)
        a->dma[i] = PWM_MID;
    a->prev = PWM_MID;
    a->volume = 256;
    a->isr_gain = 256;
    a->fade = FADE;
    a->next_slot = 2; /* slots 0 and 1 are the initial (silent) buffer */
    a->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    a->storage = furi_record_open(RECORD_STORAGE);
    a->file = storage_file_alloc(a->storage);
    a->caller = furi_thread_get_current_id();
    a->thread = furi_thread_alloc_ex("SrAudio", 1536, audio_thread, a);
    furi_thread_set_priority(a->thread, FuriThreadPriorityHighest);
    furi_thread_start(a->thread);
    furi_thread_flags_wait(FlagReady, FuriFlagWaitAny, FuriWaitForever);
    if(!a->ok) {
        furi_thread_join(a->thread);
        furi_thread_free(a->thread);
        storage_file_free(a->file);
        furi_record_close(RECORD_STORAGE);
        furi_mutex_free(a->mutex);
        free(a);
        return NULL;
    }
    return a;
}

void audio_stop(Audio* a) {
    if(!a) return;
    /* fade out first: the slot after the one already filled fades, so
     * wait for two slots to play (128 ms) */
    a->paused = true;
    uint32_t until = a->played + 2, t0 = furi_get_tick();
    while((int32_t)(a->played - until) < 0 && furi_get_tick() - t0 < 300)
        furi_delay_ms(5);
    furi_thread_flags_set(furi_thread_get_id(a->thread), FlagQuit);
    furi_thread_join(a->thread);
    furi_thread_free(a->thread);
    FURI_LOG_I(TAG, "stopped, %lu late slots", a->late);
    if(a->file_open) storage_file_close(a->file);
    storage_file_free(a->file);
    furi_record_close(RECORD_STORAGE);
    furi_mutex_free(a->mutex);
    free(a);
}

void audio_play(Audio* a, const char* name, uint32_t offset_ms, uint32_t loop) {
    if(!a) return;
    furi_mutex_acquire(a->mutex, FuriWaitForever);
    snprintf(a->want_name, sizeof(a->want_name), "%s", name ? name : "");
    a->want_offset = offset_ms;
    a->want_loop = loop;
    a->want_change = true;
    furi_mutex_release(a->mutex);
    furi_thread_flags_set(furi_thread_get_id(a->thread), FlagCommand);
}

void audio_set_volume(Audio* a, int volume) {
    if(a) a->volume = volume < 0 ? 0 : (volume > 256 ? 256 : volume);
}

void audio_pause(Audio* a, bool paused) {
    if(a) a->paused = paused;
}

uint32_t audio_late_slots(Audio* a) {
    return a ? a->late : 0;
}

uint32_t audio_take_fill_max(Audio* a) {
    if(!a) return 0;
    uint32_t m = a->fill_max_ms;
    a->fill_max_ms = 0;
    return m;
}
