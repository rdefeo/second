#include "runner.h"
#include "audio.h"
#include "display/dither.h"
#include "timeline.h"
#include "plat_furi.h"
#include "scenes/scene.h"

#include <furi.h>
#include <input/input.h>
#include <notification/notification_messages.h>
#include <stdio.h>
#include <string.h>

#define TAG "Runner"

#define FRAME_MS 20 /* scene update cap: 50 fps */

typedef enum {
    StepNone,
    StepQuit,
    StepPrev,
    StepNext,
} Step;

typedef struct {
    GrayDisplay* display;
    GrayConfig* cfg;
    FuriMessageQueue* queue;
    RunnerMode mode;
    Mask* setup_text;
    bool paused;
    Audio* audio; /* Play Demo and single scenes; NULL in stealth mode */
} Runner;

/* Keys reach us through our own fullscreen view port, which sits on top of
 * the (paused) menu while the demo runs. Without it the gui keeps routing
 * keys to the menu's view dispatcher, whose queue nobody reads until it
 * fills and blocks the gui thread - and with it gui_direct_draw_release(). */
static void input_callback(InputEvent* event, void* context) {
    furi_message_queue_put((FuriMessageQueue*)context, event, 0);
}

static void draw_nothing(Canvas* canvas, void* context) {
    UNUSED(canvas);
    UNUSED(context);
}

static void setup_text_update(Runner* r) {
    char buf[40];
    int n = snprintf(buf, sizeof(buf), "%u.%uHz  ", r->cfg->rate10 / 10, r->cfg->rate10 % 10);
    if(r->cfg->period == 1)
        snprintf(buf + n, sizeof(buf) - n, "1-bit dither");
    else
        snprintf(buf + n, sizeof(buf) - n, "%u gray levels", r->cfg->period + 1);
    plat_mask_free(r->setup_text);
    r->setup_text = plat_text_mask(PlatFontSecondary, buf);
}

/* Up/Down: rate +-0.1 Hz (hold for +-1 Hz). Left/Right: period. */
static void setup_handle(Runner* r, const InputEvent* e) {
    if(e->type != InputTypeShort && e->type != InputTypeRepeat) return;
    int step = e->type == InputTypeRepeat ? 10 : 1;
    GrayConfig* c = r->cfg;
    switch(e->key) {
    case InputKeyUp:
        c->rate10 = clampi(c->rate10 + step, GRAY_RATE10_MIN, GRAY_RATE10_MAX);
        break;
    case InputKeyDown:
        c->rate10 = clampi(c->rate10 - step, GRAY_RATE10_MIN, GRAY_RATE10_MAX);
        break;
    case InputKeyRight:
        c->period = dither_next_period(c->period, 1);
        break;
    case InputKeyLeft:
        c->period = dither_next_period(c->period, -1);
        break;
    default:
        return;
    }
    gray_display_set_config(r->display, c);
    setup_text_update(r);
}

static Step poll_input(Runner* r) {
    InputEvent e;
    Step step = StepNone;
    while(furi_message_queue_get(r->queue, &e, 0) == FuriStatusOk) {
        FURI_LOG_D(TAG, "input %s %s", input_get_key_name(e.key), input_get_type_name(e.type));
        if(e.key == InputKeyBack && (e.type == InputTypeShort || e.type == InputTypeLong))
            return StepQuit;
        if(r->mode == RunnerModeSetup) {
            setup_handle(r, &e);
            continue;
        }
        if(e.type == InputTypeShort || e.type == InputTypeRepeat) {
            if(e.key == InputKeyUp && r->cfg->volume < GRAY_VOLUME_MAX) r->cfg->volume++;
            if(e.key == InputKeyDown && r->cfg->volume > 1) r->cfg->volume--;
        }
        if(e.type != InputTypeShort) continue;
        if(e.key == InputKeyLeft) step = StepPrev;
        if(e.key == InputKeyRight) step = StepNext;
        if(e.key == InputKeyOk) r->paused = !r->paused;
    }
    return step;
}

static void music_sync(Audio* audio, const Schedule* s, int32_t global, const char** playing, bool force);

/* Runs one scene until the user steps away from it, looping it. With music
 * (sched set up by single_music_start, music_zero >= 0) the demo's music
 * plays from where the scene starts in it, back in step on every loop. */
static Step run_scene(Runner* r, const Scene* scene, const Schedule* sched, int32_t music_zero) {
    void* state = scene->init(scene);
    FURI_LOG_I(
        TAG, "scene %s %s, heap %u", scene->name, state ? "ok" : "FAILED", memmgr_get_free_heap());

    uint32_t duration = scene_duration(scene, state);
    uint32_t start = furi_get_tick();
    uint32_t pause_at = 0;
    uint32_t frames = 0, fps_at = start;
    Step step = StepNone;
    bool music = sched && music_zero >= 0;
    const char* playing = (const char*)1; /* nothing selected yet */
    bool resync = true;

    while(step == StepNone) {
        uint32_t frame_start = furi_get_tick();
        bool was_paused = r->paused;
        step = poll_input(r);
        if(step != StepNone) break;
        if(r->paused != was_paused) {
            if(r->paused)
                pause_at = frame_start;
            else
                start += frame_start - pause_at;
            if(music) audio_pause(r->audio, r->paused);
            resync = !r->paused;
        }

        uint32_t t = (r->paused ? pause_at : frame_start) - start;
        if(t >= duration) {
            start = frame_start; /* single scene: loop */
            t = 0;
            resync = true;
        }
        if(music && !r->paused) {
            music_sync(r->audio, sched, music_zero + (int32_t)t, &playing, resync);
            resync = false;
            audio_set_volume(r->audio, r->cfg->volume * 256 / GRAY_VOLUME_MAX);
        }

        Frame* f = gray_display_frame(r->display);
        if(!state) {
            scene_draw_error(f, scene->name);
        } else {
            scene->render(state, f, t);
            if(r->mode == RunnerModeSetup && r->setup_text)
                gfx_blit_mask(f, r->setup_text, (FB_W - r->setup_text->w) / 2, 53, 255);
        }
        gray_display_present(r->display);

        frames++;
        if(frame_start - fps_at >= 5000) {
            FURI_LOG_D(
                TAG,
                "%s: %lu fps, late subframes %lu, simd check %d",
                scene->name,
                frames * 1000 / (frame_start - fps_at),
                gray_display_late_count(r->display),
                gray_display_simd_check());
            frames = 0;
            fps_at = frame_start;
        }

        uint32_t spent = furi_get_tick() - frame_start;
        furi_delay_ms(spent < FRAME_MS ? FRAME_MS - spent : 1);
    }

    if(state) scene->deinit(state);
    return step;
}

/* music0's second half ends with a jump back to (file) order 33, so the
 * end scroller's music repeats until the demo is left: s0b.pcm goes on
 * from this sample at its end (printed by tools/render_music.py). */
#define S0B_LOOP 1679187

/* Which music should be playing at global time `global`: music0's first
 * half until it stops during the title, then music1 from the moment its
 * segment starts, then music0's second half. Returns the stream name and
 * sets *offset; NULL means silence. */
static const char* music_at(const Schedule* s, int32_t global, uint32_t* offset) {
    if(s->seg[SEG_S0B] >= 0 && global >= s->seg[SEG_S0B]) {
        *offset = (uint32_t)(global - s->seg[SEG_S0B]);
        return "s0b.pcm";
    }
    if(s->seg[SEG_S1] >= 0 && global >= s->seg[SEG_S1]) {
        *offset = (uint32_t)(global - s->seg[SEG_S1]);
        return "s1.pcm";
    }
    if(global < SYNC_MUSIC0_STOP) {
        *offset = (uint32_t)global;
        return "s0a.pcm";
    }
    *offset = 0;
    return NULL;
}

/* Start/switch the stream when the wanted one changes (or when forced,
 * after a jump or a pause, to re-seek). */
static void music_sync(Audio* audio, const Schedule* s, int32_t global, const char** playing, bool force) {
    uint32_t offset;
    const char* want = music_at(s, global, &offset);
    if(force || want != *playing) {
        audio_play(audio, want, offset, want && !strcmp(want, "s0b.pcm") ? S0B_LOOP : AUDIO_NO_LOOP);
        *playing = want;
    }
}

/* Where scene idx starts in the demo's music, to play it on its own: at
 * its cue, or for a scene that follows the one before it, at that one's
 * start plus its duration (a scene whose duration depends on its data is
 * loaded briefly to ask). Sets sched up so music_at() works on that
 * clock, and returns the scene's start on it. */
static int32_t single_music_start(Schedule* sched, size_t idx) {
    int32_t offset = 0;
    size_t j = idx;
    while(j > 0 && scene_cue[j].seg == CUE_AFTER) {
        const Scene* prev = scene_list[--j];
        uint32_t d = prev->duration_ms;
        if(prev->duration_fn) {
            void* st = prev->init(prev);
            d = scene_duration(prev, st);
            if(st) prev->deinit(st);
        }
        offset += (int32_t)d + scene_cue[j + 1].ms;
    }
    schedule_reset(sched);
    if(scene_cue[j].seg != SEG_S0A) sched->seg[scene_cue[j].seg] = 0;
    return scene_cue[j].ms + offset;
}

/* Play All: one clock for the whole demo. Each scene starts at its cue
 * (see scene_cue) and runs until the next scene's start, holding its last
 * frame if it ends early. Left/Right jump to the previous/next start. */
static void run_play_all(Runner* r) {
    Schedule sched;
    schedule_reset(&sched);
    Audio* audio = audio_start();
    /* Draw below normal priority so the storage service, which feeds the
     * audio from the SD card, never waits behind a heavy scene. */
    FuriThreadPriority prio = furi_thread_get_current_priority();
    furi_thread_set_current_priority(FuriThreadPriorityLow);
    FURI_LOG_I(TAG, "play all: priority %d -> %d", prio, furi_thread_get_current_priority());
    const char* playing = (const char*)1; /* nothing selected yet */
    bool resync = true;
    uint32_t base = furi_get_tick(); /* tick at global time 0 */
    uint32_t pause_at = 0;
    size_t idx = 0;

    while(idx < scene_count) {
        const Scene* scene = scene_list[idx];
        void* state = scene->init(scene);
        uint32_t duration = scene_duration(scene, state);
        int32_t zero = sched.zero[idx];
        int32_t next = schedule_enter(&sched, idx, duration);
        FURI_LOG_I(
            TAG,
            "play all: %s at %ld ms, lasts %lu, next at %ld, heap %u",
            scene->name,
            zero,
            duration,
            next,
            memmgr_get_free_heap());

        Step step = StepNone;
        bool jumped = false;
        uint32_t frames = 0, fps_at = furi_get_tick();
        for(;;) {
            uint32_t now = furi_get_tick();
            bool was_paused = r->paused;
            step = poll_input(r);
            if(step != StepNone) {
                jumped = true;
                break;
            }
            if(r->paused != was_paused) {
                if(r->paused)
                    pause_at = now;
                else
                    base += now - pause_at;
                audio_pause(audio, r->paused);
                resync = !r->paused;
            }
            int32_t global = (int32_t)((r->paused ? pause_at : now) - base);
            if(global >= next) {
                step = StepNext;
                break;
            }
            if(!r->paused) {
                music_sync(audio, &sched, global, &playing, resync);
                resync = false;
                /* the last scene fades the music out over its final 2 s */
                int32_t left = next - global;
                int vol = r->cfg->volume * 256 / GRAY_VOLUME_MAX;
                if(idx + 1 == scene_count && left < 2000) vol = vol * left / 2000;
                audio_set_volume(audio, vol);
            }
            int32_t t = clampi(global - zero, 0, duration ? (int32_t)duration - 1 : 0);

            Frame* f = gray_display_frame(r->display);
            if(state)
                scene->render(state, f, (uint32_t)t);
            else
                scene_draw_error(f, scene->name);
            gray_display_present(r->display);

            frames++;
            if(now - fps_at >= 5000) {
                FURI_LOG_D(
                    TAG,
                    "%s%s: %lu fps, prio %d, audio late slots %lu, slowest refill %lu ms, heap %u/%u",
                    scene->name,
                    state ? "" : " FAILED",
                    frames * 1000 / (now - fps_at),
                    furi_thread_get_current_priority(),
                    audio_late_slots(audio),
                    audio_take_fill_max(audio),
                    memmgr_get_free_heap(),
                    memmgr_heap_get_max_free_block());
                frames = 0;
                fps_at = now;
            }
            uint32_t spent = furi_get_tick() - now;
            furi_delay_ms(spent < FRAME_MS ? FRAME_MS - spent : 1);
        }
        if(state) scene->deinit(state);

        if(step == StepQuit) break;
        if(jumped) resync = true; /* the music must follow the clock jump */
        /* jumps move the global clock to the target scene's start */
        uint32_t now = r->paused ? pause_at : furi_get_tick();
        if(step == StepPrev) {
            idx = idx ? idx - 1 : 0;
            base = now - (uint32_t)sched.zero[idx];
        } else {
            if(idx + 1 < scene_count) base = now - (uint32_t)next;
            idx++;
        }
    }
    audio_stop(audio);
    gray_config_save(r->cfg); /* remembers the volume */
    furi_thread_set_current_priority(prio);
}

void runner_run(Gui* gui, GrayConfig* cfg, RunnerMode mode, size_t start) {
    Runner r = {.cfg = cfg, .mode = mode};
    r.queue = furi_message_queue_alloc(16, sizeof(InputEvent));
    ViewPort* input_port = view_port_alloc();
    view_port_draw_callback_set(input_port, draw_nothing, NULL);
    view_port_input_callback_set(input_port, input_callback, r.queue);
    gui_add_view_port(gui, input_port, GuiLayerFullscreen);
    NotificationApp* notifications = furi_record_open(RECORD_NOTIFICATION);
    notification_message(notifications, &sequence_display_backlight_enforce_on);

    r.display = gray_display_start(gui, cfg);
    plat_furi_set_display(r.display);

    if(mode == RunnerModeSetup) {
        setup_text_update(&r);
        run_scene(&r, &scene_test_pattern, NULL, -1);
        gray_config_save(cfg);
        plat_mask_free(r.setup_text);
    } else if(mode == RunnerModePlayAll) {
        run_play_all(&r);
    } else {
        r.audio = audio_start();
        /* below normal priority, as in Play Demo: the SD card feeds the audio */
        FuriThreadPriority prio = furi_thread_get_current_priority();
        furi_thread_set_current_priority(FuriThreadPriorityLow);
        size_t idx = start;
        while(idx < scene_count) {
            Schedule sched;
            int32_t zero = single_music_start(&sched, idx);
            Step step = run_scene(&r, scene_list[idx], &sched, zero);
            if(step == StepQuit) break;
            if(step == StepPrev) {
                idx = idx ? idx - 1 : scene_count - 1;
            } else {
                idx++;
                if(idx >= scene_count) idx = 0;
            }
        }
        audio_stop(r.audio);
        gray_config_save(cfg); /* remembers the volume */
        furi_thread_set_current_priority(prio);
    }

    plat_furi_set_display(NULL);
    gui_remove_view_port(gui, input_port);
    view_port_free(input_port);
    gray_display_stop(r.display);

    notification_message(notifications, &sequence_display_backlight_enforce_auto);
    furi_record_close(RECORD_NOTIFICATION);
    furi_message_queue_free(r.queue);
}
