#include "scene.h"
#include "../gfx/image.h"

/* Future Crew end logo: fade in from white, hold, fade out to black.
 * The picture is dark, so its mid-tones are lifted (black and white stay
 * put) before the fades are applied. */

#define FADE_IN_TICKS  128
#define FADE_OUT_AT    500
#define FADE_OUT_TICKS 32
#define ENDLOGO_MS     7600

static void* endlogo_init(const Scene* self) {
    (void)self;
    Image* img = plat_alloc(sizeof(Image));
    if(img && !image_load(img, "endlogo.srg")) {
        plat_free(img);
        return NULL;
    }
    return img;
}

static void endlogo_render(void* state, Frame* f, uint32_t t_ms) {
    Image* img = state;
    int32_t tick = (int32_t)frame70(t_ms);
    uint8_t lut[256];
    if(tick < FADE_IN_TICKS)
        lut_fade_white(lut, 256 - tick * 256 / FADE_IN_TICKS);
    else if(tick < FADE_OUT_AT)
        lut_fade_white(lut, 0);
    else
        lut_fade_black(lut, clampi((tick - FADE_OUT_AT) * 256 / FADE_OUT_TICKS, 0, 256));
    /* in place is safe: the curve never maps i below i */
    for(int i = 0; i < 256; i++)
        lut[i] = lut[i * (510 - i) / 255];
    gfx_letterbox(f, VIEW_43, lut[0]);
    image_draw(f, img, 0, VIEW_43.x, VIEW_43.y, lut);
}

static void endlogo_deinit(void* state) {
    image_free(state);
    plat_free(state);
}

const Scene scene_endlogo = {
    .name = "End Logo",
    .duration_ms = ENDLOGO_MS,
    .init = endlogo_init,
    .render = endlogo_render,
    .deinit = endlogo_deinit,
};
