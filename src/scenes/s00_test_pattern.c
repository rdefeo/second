#include "scene.h"

/* Gray ramp for tuning the display: top half is 8 flat steps, then a smooth
 * gradient, then a checkerboard next to 50% gray. With the subframe rate
 * matched to the panel the steps hold still; when it's off they pulse or
 * roll. */

static void* test_init(const Scene* self) {
    (void)self;
    return (void*)1;
}

static void test_render(void* state, Frame* f, uint32_t t_ms) {
    (void)state;
    (void)t_ms;
    for(int i = 0; i < 8; i++)
        gfx_fill_rect(f, i * 16, 0, 16, 24, (uint8_t)(i * 255 / 7));
    for(int x = 0; x < FB_W; x++)
        gfx_fill_rect(f, x, 24, 1, 16, (uint8_t)(x * 255 / (FB_W - 1)));
    for(int y = 40; y < 52; y++)
        for(int x = 0; x < 64; x++)
            f->px[y][x] = ((x ^ y) & 1) ? 255 : 0;
    gfx_fill_rect(f, 64, 40, 64, 12, 128);
    gfx_fill_rect(f, 0, 52, FB_W, 12, 0); /* room for the setup text */
}

static void test_deinit(void* state) {
    (void)state;
}

const Scene scene_test_pattern = {
    .name = "Test Pattern",
    .duration_ms = 0xFFFFFFFFu,
    .init = test_init,
    .render = test_render,
    .deinit = test_deinit,
};
