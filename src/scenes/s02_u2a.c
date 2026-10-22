#include "scene.h"
#include "u2.h"
#include "../gfx/image.h"

/* Spaceships fly in over the landscape the intro ended on (U2 engine). */

typedef struct {
    U2* u2;
    Image bg;
} U2A;

static void* u2a_init(const Scene* self) {
    (void)self;
    U2A* s = plat_alloc(sizeof(U2A));
    if(!s) return NULL;
    if(!image_load(&s->bg, "alku_bg.srg") || !(s->u2 = u2_load("u2a"))) {
        image_free(&s->bg);
        plat_free(s);
        return NULL;
    }
    return s;
}

static uint32_t u2a_duration(void* state) {
    U2A* s = state;
    return (uint32_t)u2_frames(s->u2) * 1000 / 70;
}

static void u2a_render(void* state, Frame* f, uint32_t t_ms) {
    U2A* s = state;
    /* the intro's landscape where its pan ended (same band mapping) */
    image_draw(f, &s->bg, 0, FB_W - s->bg.w, 0, NULL);
    u2_seek(s->u2, (int32_t)frame70(t_ms));
    u2_draw(s->u2, f);
}

static void u2a_deinit(void* state) {
    U2A* s = state;
    u2_free(s->u2);
    image_free(&s->bg);
    plat_free(s);
}

const Scene scene_u2a = {
    .name = "U2A Ships",
    .duration_ms = 7500,
    .init = u2a_init,
    .render = u2a_render,
    .deinit = u2a_deinit,
    .duration_fn = u2a_duration,
};
