#include "gray.h"
#include "dither.h"

#include <furi.h>
#include <furi_hal_cortex.h>
#include <stm32wbxx.h>
#include <storage/storage.h>

#define TAG "GrayDisplay"

#define CONFIG_PATH  APP_DATA_PATH("display.conf")
#define CONFIG_MAGIC 0x31445253u /* "SRD1" */

typedef struct {
    uint32_t magic;
    uint16_t rate10;
    uint8_t period;
    uint8_t volume; /* 0 in files from before volume existed */
} GrayConfigFile;

struct GrayDisplay {
    Gui* gui;
    Canvas* canvas;
    FuriThread* thread;
    FuriMutex* mutex;
    volatile bool quit;

    GrayConfig cfg;
    bool cfg_dirty;
    DitherThresholds thr[DITHER_MAX_PERIOD];

    Frame* back; /* written by the scene */
    Frame* front; /* read by the subframe thread, under mutex */
    volatile uint32_t late;
};

static void sanitize(GrayConfig* cfg) {
    if(cfg->rate10 < GRAY_RATE10_MIN || cfg->rate10 > GRAY_RATE10_MAX)
        cfg->rate10 = GRAY_RATE10_DEFAULT;
    cfg->period = dither_valid_period(cfg->period ? cfg->period : GRAY_PERIOD_DEFAULT);
    if(cfg->volume < 1 || cfg->volume > GRAY_VOLUME_MAX) cfg->volume = GRAY_VOLUME_MAX;
}

void gray_config_load(GrayConfig* cfg) {
    cfg->rate10 = GRAY_RATE10_DEFAULT;
    cfg->period = GRAY_PERIOD_DEFAULT;
    cfg->volume = GRAY_VOLUME_MAX;

    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* file = storage_file_alloc(storage);
    GrayConfigFile data;
    if(storage_file_open(file, CONFIG_PATH, FSAM_READ, FSOM_OPEN_EXISTING) &&
       storage_file_read(file, &data, sizeof(data)) == sizeof(data) &&
       data.magic == CONFIG_MAGIC) {
        cfg->rate10 = data.rate10;
        cfg->period = data.period;
        cfg->volume = data.volume;
    }
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);
    sanitize(cfg);
}

bool gray_config_save(const GrayConfig* cfg) {
    GrayConfigFile data = {
        .magic = CONFIG_MAGIC, .rate10 = cfg->rate10, .period = cfg->period, .volume = cfg->volume};
    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* file = storage_file_alloc(storage);
    bool ok = storage_file_open(file, CONFIG_PATH, FSAM_WRITE, FSOM_CREATE_ALWAYS) &&
              storage_file_write(file, &data, sizeof(data)) == sizeof(data);
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);
    return ok;
}

static void rebuild_thresholds(GrayDisplay* d) {
    for(uint8_t s = 0; s < d->cfg.period; s++)
        dither_thresholds(d->cfg.period, s, d->thr[s]);
    d->cfg_dirty = false;
}

static int32_t gray_thread(void* context) {
    GrayDisplay* d = context;
    const uint32_t cpu_us = furi_hal_cortex_instructions_per_microsecond();
    uint32_t next = DWT->CYCCNT;
    uint8_t sub = 0;

    while(!d->quit) {
        next += (uint32_t)((uint64_t)cpu_us * 10000000ULL / d->cfg.rate10);

        /* Sleep until 0.5..1.5 ms before the deadline (a tick sleep can end
         * up to 1 ms early): enough to build a subframe, little to spin. */
        int32_t wait_us = (int32_t)(next - DWT->CYCCNT) / (int32_t)cpu_us;
        if(wait_us > 1500) {
            furi_delay_ms((wait_us - 500) / 1000);
        } else if(wait_us < -20000) {
            next = DWT->CYCCNT; /* fell way behind (SD access, debugger): resync */
            d->late++;
        }

        furi_mutex_acquire(d->mutex, FuriWaitForever);
        if(d->cfg_dirty) rebuild_thresholds(d);
        if(sub >= d->cfg.period) sub = 0;
        dither_subframe_pages(d->front, d->thr[sub], canvas_get_buffer(d->canvas));
        while((int32_t)(next - DWT->CYCCNT) > 0) {
        }
        canvas_commit(d->canvas);
        furi_mutex_release(d->mutex);

        sub++;
    }
    return 0;
}

/* One-time check that the SIMD subframe builder matches the portable one,
 * for every subframe of every period, on a frame with all pixel values. */
static int simd_result = -1; /* -1 untested, 0 mismatch, 1 ok */

int gray_display_simd_check(void) {
    return simd_result;
}

static void simd_self_test(Frame* f) {
    for(int y = 0; y < FB_H; y++)
        for(int x = 0; x < FB_W; x++)
            f->px[y][x] = (uint8_t)(x * 2 + y * 37);
    uint8_t* a = malloc(FB_W * FB_H / 8);
    uint8_t* b = malloc(FB_W * FB_H / 8);
    DitherThresholds thr;
    bool ok = true;
    for(uint8_t p = 1; p <= DITHER_MAX_PERIOD && ok; p = dither_next_period(p, 1) == p ? 99 : dither_next_period(p, 1)) {
        for(uint8_t s = 0; s < p && ok; s++) {
            dither_thresholds(p, s, thr);
            dither_subframe_pages(f, thr, a);
            dither_subframe_pages_c(f, thr, b);
            ok = memcmp(a, b, FB_W * FB_H / 8) == 0;
        }
    }
    FURI_LOG_I(TAG, "SIMD subframe builder %s", ok ? "matches" : "MISMATCH");
    simd_result = ok;
    free(a);
    free(b);
    memset(f, 0, sizeof(Frame));
}

GrayDisplay* gray_display_start(Gui* gui, const GrayConfig* cfg) {
    GrayDisplay* d = malloc(sizeof(GrayDisplay));
    memset(d, 0, sizeof(GrayDisplay));
    d->gui = gui;
    d->cfg = *cfg;
    sanitize(&d->cfg);
    d->cfg_dirty = true;
    d->back = malloc(sizeof(Frame));
    d->front = malloc(sizeof(Frame));
    memset(d->back, 0, sizeof(Frame));
    memset(d->front, 0, sizeof(Frame));
    static bool tested = false;
    if(!tested) {
        simd_self_test(d->back);
        tested = true;
    }
    d->mutex = furi_mutex_alloc(FuriMutexTypeNormal);

    d->canvas = gui_direct_draw_acquire(gui);

    d->thread = furi_thread_alloc_ex("SrGray", 1024, gray_thread, d);
    furi_thread_set_priority(d->thread, FuriThreadPriorityHigh);
    furi_thread_start(d->thread);
    return d;
}

void gray_display_stop(GrayDisplay* d) {
    d->quit = true;
    furi_thread_join(d->thread);
    furi_thread_free(d->thread);
    gui_direct_draw_release(d->gui);
    furi_mutex_free(d->mutex);
    free(d->back);
    free(d->front);
    free(d);
}

void gray_display_set_config(GrayDisplay* d, const GrayConfig* cfg) {
    furi_mutex_acquire(d->mutex, FuriWaitForever);
    d->cfg = *cfg;
    sanitize(&d->cfg);
    d->cfg_dirty = true;
    furi_mutex_release(d->mutex);
}

Frame* gray_display_frame(GrayDisplay* d) {
    return d->back;
}

void gray_display_present(GrayDisplay* d) {
    furi_mutex_acquire(d->mutex, FuriWaitForever);
    memcpy(d->front, d->back, sizeof(Frame));
    furi_mutex_release(d->mutex);
}

Canvas* gray_display_lock_canvas(GrayDisplay* d) {
    furi_mutex_acquire(d->mutex, FuriWaitForever);
    return d->canvas;
}

void gray_display_unlock_canvas(GrayDisplay* d) {
    furi_mutex_release(d->mutex);
}

uint32_t gray_display_late_count(GrayDisplay* d) {
    return d->late;
}
