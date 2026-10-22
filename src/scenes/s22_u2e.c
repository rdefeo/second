#include "scene.h"
#include "../timeline.h"
#include "u2.h"

/* Flight through the polygon city (U2 engine), animated at 35 fps. The
 * first frame holds until the music's U2E_ANIM sync. */

#define HOLD_MS (SYNC_U2E_ANIM - SYNC_U2E_START)

typedef struct {
    U2* u2;
} U2E;

static void* u2e_init(const Scene* self) {
    (void)self;
    U2E* s = plat_alloc(sizeof(U2E));
    if(!s) return NULL;
    if(!(s->u2 = u2_load("u2e"))) {
        plat_free(s);
        return NULL;
    }
    return s;
}

static uint32_t u2e_duration(void* state) {
    U2E* s = state;
    return HOLD_MS + (uint32_t)u2_frames(s->u2) * 1000 / 35;
}

static void u2e_render(void* state, Frame* f, uint32_t t_ms) {
    U2E* s = state;
    gfx_clear(f, 0);
    int32_t frame = t_ms < HOLD_MS ? 0 : (int32_t)((t_ms - HOLD_MS) * 35 / 1000);
    u2_seek(s->u2, frame);
    u2_draw(s->u2, f);
}

static void u2e_deinit(void* state) {
    U2E* s = state;
    u2_free(s->u2);
    plat_free(s);
}

const Scene scene_u2e = {
    .name = "U2E City",
    .duration_ms = 55000,
    .init = u2e_init,
    .render = u2e_render,
    .deinit = u2e_deinit,
    .duration_fn = u2e_duration,
};
