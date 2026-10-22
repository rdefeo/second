#include "scene.h"
#include "../timeline.h"
#include "../gfx/image.h"

#include <string.h>

/* A sword scrolls through a ray-traced reflective scene: like Forest, the
 * original's position tables (inverted offline) say which pixel of the
 * scrolling sword strip appears at each screen pixel. The picture fades
 * in, the sword passes through, and it fades out. The original is very
 * dark, so mid-tones are lifted (black and white stay put). */

#define SCROLL_MS  (SYNC_WATER_SCROLL - SYNC_WATER_START)
#define FADEOUT_MS (SYNC_WATER_FADEOUT - SYNC_WATER_START)
#define WATER_MS   (FADEOUT_MS + 64 * 1000 / 70)
#define LAST_COL   390

typedef struct {
    Image bg;
    uint8_t* blob;
    uint16_t win_w, win_h, sword_w, sword_h;
    const uint8_t* map; /* 85x64 u16 window pixel or 0xffff */
    const uint8_t* sword; /* brightness, 0 = transparent */
} Water;

static void* water_init(const Scene* self) {
    (void)self;
    Water* s = plat_alloc(sizeof(Water));
    if(!s) return NULL;
    size_t size = 0;
    s->blob = plat_load_asset("water.bin", &size);
    if(!s->blob || size < 12 || memcmp(s->blob, "SRWA", 4) || !image_load(&s->bg, "water.srg")) {
        image_free(&s->bg);
        plat_free(s->blob);
        plat_free(s);
        return NULL;
    }
    const uint8_t* d = s->blob;
    s->win_w = d[4] | d[5] << 8;
    s->win_h = d[6] | d[7] << 8;
    s->sword_w = d[8] | d[9] << 8;
    s->sword_h = d[10] | d[11] << 8;
    s->map = d + 12;
    s->sword = s->map + s->bg.w * s->bg.h * 2;
    return s;
}

static void water_render(void* state, Frame* f, uint32_t t_ms) {
    Water* s = state;
    const Rect v = VIEW_43;
    int32_t tick = (int32_t)frame70(t_ms);
    int level = clampi(tick * 256 / 63, 0, 256);
    if(t_ms >= FADEOUT_MS) level = clampi(256 - (int32_t)frame70(t_ms - FADEOUT_MS) * 256 / 63, 0, 256);
    /* the sword enters at the window's right edge, one column per 3 ticks */
    int32_t step = t_ms < SCROLL_MS ? 0 : (int32_t)frame70(t_ms - SCROLL_MS) / 3;

    const uint8_t* bg = image_frame(&s->bg, 0);
    gfx_clear(f, 0);
    for(int y = 0; y < v.h; y++) {
        for(int x = 0; x < v.w; x++) {
            int i = y * v.w + x;
            int px = bg[i];
            uint16_t k = s->map[i * 2] | s->map[i * 2 + 1] << 8;
            if(k != 0xffff && step > 0) {
                int r = k / s->win_w, c = k % s->win_w;
                int col = c - s->win_w + step;
                if(col > LAST_COL) col = LAST_COL;
                if(r < s->sword_h && col >= 0) {
                    int sv = s->sword[r * s->sword_w + col];
                    if(sv) px = sv;
                }
            }
            px = px * (510 - px) / 255; /* lift the dark scene's mid-tones */
            f->px[v.y + y][v.x + x] = (uint8_t)(px * level >> 8);
        }
    }
}

static void water_deinit(void* state) {
    Water* s = state;
    image_free(&s->bg);
    plat_free(s->blob);
    plat_free(s);
}

const Scene scene_water = {
    .name = "Water",
    .duration_ms = WATER_MS,
    .init = water_init,
    .render = water_render,
    .deinit = water_deinit,
};
