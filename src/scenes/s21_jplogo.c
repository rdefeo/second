#include "scene.h"
#include "../gfx/image.h"

/* Knight picture: falls into view from the top with acceleration, then behaves
 * like jelly: its top and bottom edges are two masses joined by a spring,
 * falling onto the floor and bouncing until they settle. While squashed,
 * each line is stretched sideways by a sine of its height. */

#define SIM_STEPS   200 /* spring simulation steps, 4 ticks each */
#define JELLY_TICKS 700
#define FREEZE_TICK 511
#define MAX_SCROLL  128
#define REST        (400 * 16) /* picture height, 1/16 px */

typedef struct {
    Image img;
    int16_t top[SIM_STEPS + 1], bottom[SIM_STEPS + 1]; /* 1/16 px */
    int16_t scroll[MAX_SCROLL];
    int scroll_len;
} Jelly;

static void simulate(Jelly* j) {
    float y1 = 0, y2 = 399 * 16, v1 = 500, v2 = 500;
    float restitution = 1, prev_stretch = 0;
    bool settled = false;
    for(int s = 0; s < SIM_STEPS; s++) {
        if(!settled) {
            y1 += v1;
            y2 += v2;
            v2 += 16; /* gravity */
            if(y2 > REST) { /* bottom hits the floor */
                y2 -= v2;
                v2 = -v2 * restitution / 8;
                if(restitution < 4) restitution += 3;
            }
            v1 += 16;
            float stretch = (y2 - y1) - REST;
            if((stretch < 0) != (prev_stretch < 0)) v1 = v1 * 7 / 8; /* damping */
            prev_stretch = stretch;
            v1 += stretch / 8; /* spring */
            v2 -= stretch / 8;
        }
        if(s > 90) { /* ease the bottom onto the floor and lock */
            if(y2 >= 399 * 16) {
                y2 = REST;
                settled = true;
            } else {
                v2 = 8;
            }
            y1 = y2 - REST;
        }
        j->top[s] = (int16_t)y1;
        j->bottom[s] = (int16_t)y2;
    }
    j->top[SIM_STEPS] = j->top[SIM_STEPS - 1];
    j->bottom[SIM_STEPS] = j->bottom[SIM_STEPS - 1];

    float y = 400, speed = 64;
    j->scroll_len = 0;
    while(y > 0 && j->scroll_len < MAX_SCROLL) {
        y -= speed / 64;
        speed += 6;
        if(y < 0) y = 0;
        j->scroll[j->scroll_len++] = (int16_t)y;
    }
}

static void* jplogo_init(const Scene* self) {
    (void)self;
    Jelly* j = plat_alloc(sizeof(Jelly));
    if(!j) return NULL;
    if(!image_load(&j->img, "jplogo.srg")) {
        plat_free(j);
        return NULL;
    }
    simulate(j);
    return j;
}

/* Draw source row `row` (0..399 original) stretched to `width` original px,
 * centred, on screen row y. */
static void line_zoom(Frame* f, const Image* img, int y, int row, int width) {
    int w = width * VIEW_43.w / 320;
    if(w < 1) return;
    int x0 = FB_W / 2 - w / 2;
    const uint8_t* src = image_frame(img, 0) + clampi(row * img->h / 400, 0, img->h - 1) * img->w;
    for(int x = 0; x < w; x++)
        gfx_pset(f, x0 + x, y, src[x * img->w / w]);
}

static void jplogo_render(void* state, Frame* f, uint32_t t_ms) {
    Jelly* j = state;
    int32_t tick = (int32_t)frame70(t_ms);
    gfx_clear(f, 0);

    if(tick < j->scroll_len) {
        int yy = j->scroll[tick];
        for(int y = 0; y < FB_H; y++) {
            int row = (int)((y + 0.5f) * 400 / FB_H) + yy;
            if(row >= 0 && row < 400) line_zoom(f, &j->img, y, row, 184);
        }
        return;
    }

    int32_t k = tick - j->scroll_len;
    if(k > FREEZE_TICK) k = FREEZE_TICK;
    /* interpolate the simulation (one step per 4 ticks) */
    int s = k / 4, c = k & 3;
    int y1 = (j->top[s] * (3 - c) + j->top[s + 1] * c) / 3 / 16;
    int y2 = (j->bottom[s] * (3 - c) + j->bottom[s + 1] * c) / 3 / 16;
    if(y2 <= y1) return;
    int squash = (400 - (y2 - y1)) / 8;

    for(int y = 0; y < FB_H; y++) {
        int oy = (int)((y + 0.5f) * 400 / FB_H);
        if(oy < y1 || oy >= y2) continue;
        int row = (oy - y1) * 400 / (y2 - y1);
        int width = 184 + (fx_sin(row * 32 / 25) * 255 / FX_ONE14 * squash + 32) / 64;
        line_zoom(f, &j->img, y, row, width & ~1);
    }
}

static void jplogo_deinit(void* state) {
    Jelly* j = state;
    image_free(&j->img);
    plat_free(j);
}

const Scene scene_jplogo = {
    .name = "JP Logo",
    .duration_ms = 11200,
    .init = jplogo_init,
    .render = jplogo_render,
    .deinit = jplogo_deinit,
};
