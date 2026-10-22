#pragma once

#include <stddef.h>
#include <stdint.h>

#include "../gfx/gfx.h"

typedef struct Scene Scene;

/* A scene is a pure function of time: render(t) must draw the whole frame
 * for t milliseconds after the scene started. Scenes with simulation state
 * step it forward internally to t. No furi headers here, so every scene
 * also builds in the desktop harness (host/). */
struct Scene {
    const char* name;
    uint32_t duration_ms;
    /* Returns scene state, or NULL if the scene could not start (out of
     * memory, missing asset). */
    void* (*init)(const Scene* self);
    void (*render)(void* state, Frame* f, uint32_t t_ms);
    void (*deinit)(void* state);
    /* Optional: length known only after init (e.g. depends on asset
     * data). Overrides duration_ms when set. */
    uint32_t (*duration_fn)(void* state);
};

static inline uint32_t scene_duration(const Scene* s, void* state) {
    return (s->duration_fn && state) ? s->duration_fn(state) : s->duration_ms;
}

/* Demo order; this is also the menu order. */
extern const Scene* const scene_list[];
extern const size_t scene_count;

/* Play All schedule: when each scene starts. Either a point in one of the
 * three music segments (SEG_* in timeline.h) or, with seg = CUE_AFTER,
 * right after the previous scene ends (+ms). A scene that ends before the
 * next one starts holds its last frame. */
#define CUE_AFTER (-1)

typedef struct {
    int8_t seg;
    int32_t ms;
} SceneCue;

extern const SceneCue scene_cue[];

/* The music segments themselves start when a given scene ends. */
#define SEG_S1_AFTER_SCENE  3 /* music1 starts when the title picture ends */
#define SEG_S0B_AFTER_SCENE 20 /* music0 resumes after the JP logo... */
#define SEG_S0B_DELAY       1800 /* ...and a short pause */

#define SCHEDULE_MAX 32

/* Play All clock bookkeeping, filled in as scenes are entered. */
typedef struct {
    int32_t zero[SCHEDULE_MAX + 1]; /* global ms each scene starts */
    int32_t seg[3]; /* global ms each music segment starts (-1 = not yet) */
} Schedule;

void schedule_reset(Schedule* s);
/* Scene idx starts now (at zero[idx]) and lasts `duration`: works out when
 * the next scene starts, records it in zero[idx + 1] and returns it. */
int32_t schedule_enter(Schedule* s, size_t idx, uint32_t duration);

/* Gray ramp used by Display Setup. */
extern const Scene scene_test_pattern;

/* Draws "not available" for scenes that failed to init. */
void scene_draw_error(Frame* f, const char* name);
