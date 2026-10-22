#include "scene.h"
#include "../gfx/image.h"

/* The face picture is revealed row by row: in every row two edges start
 * on a slanted line and run outward, the right edge faster on lower rows
 * and the left edge faster on upper rows, giving a jagged opening. */

#define REVEAL_TICKS 80
#define STEPS_PER_TICK 6
#define LENS_TRANS_MS 4300

static void* lens_trans_init(const Scene* self) {
    (void)self;
    Image* img = plat_alloc(sizeof(Image));
    if(img && !image_load(img, "lens.srg")) {
        plat_free(img);
        return NULL;
    }
    return img;
}

static void lens_trans_render(void* state, Frame* f, uint32_t t_ms) {
    Image* img = state;
    int32_t steps = (int32_t)frame70(t_ms) * STEPS_PER_TICK;
    if(steps > REVEAL_TICKS * STEPS_PER_TICK) steps = REVEAL_TICKS * STEPS_PER_TICK;

    gfx_clear(f, 0);
    const Rect v = VIEW_43;
    for(int y = 0; y < v.h; y++) {
        int b = y * 200 / v.h; /* original row */
        /* edges in original pixels, speeds in 1/64 px per step */
        float start = 170.0f + (100 - b) * 50 / 64.0f;
        float right = start + (float)(((19 + b / 5 + 4) & ~7) * steps) / 64.0f;
        float left = start - (float)(((19 + (199 - b) / 5 + 4) & ~7) * steps) / 64.0f;
        int x0 = clampi((int)(left * v.w / 320), 0, v.w);
        int x1 = clampi((int)(right * v.w / 320) + 1, 0, v.w);
        const uint8_t* src = image_frame(img, 0) + y * img->w;
        for(int x = x0; x < x1; x++)
            f->px[v.y + y][v.x + x] = src[x];
    }
}

static void lens_trans_deinit(void* state) {
    image_free(state);
    plat_free(state);
}

const Scene scene_lens_trans = {
    .name = "Lens Transition",
    .duration_ms = LENS_TRANS_MS,
    .init = lens_trans_init,
    .render = lens_trans_render,
    .deinit = lens_trans_deinit,
};
