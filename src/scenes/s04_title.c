#include "scene.h"
#include "../gfx/image.h"

/* "Second Reality" title picture: holds white (after the explosion flash),
 * fades in from white over 128 ticks, then holds while music0 stops. */

#define WAIT_TICKS 32
#define FADE_TICKS 128
#define TITLE_MS   4030

static void* title_init(const Scene* self) {
    (void)self;
    Image* img = plat_alloc(sizeof(Image));
    if(img && !image_load(img, "title.srg")) {
        plat_free(img);
        return NULL;
    }
    return img;
}

static void title_render(void* state, Frame* f, uint32_t t_ms) {
    Image* img = state;
    int32_t tick = (int32_t)frame70(t_ms) - WAIT_TICKS;
    int k = tick < 0 ? 256 : clampi(256 - tick * 256 / FADE_TICKS, 0, 256);
    uint8_t lut[256];
    lut_fade_white(lut, k);
    gfx_letterbox(f, VIEW_43, lut[0]);
    image_draw(f, img, 0, VIEW_43.x, VIEW_43.y, lut);
}

static void title_deinit(void* state) {
    image_free(state);
    plat_free(state);
}

const Scene scene_title = {
    .name = "Title Logo",
    .duration_ms = TITLE_MS,
    .init = title_init,
    .render = title_render,
    .deinit = title_deinit,
};
