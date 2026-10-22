#include "scene.h"
#include "../timeline.h"
#include "../gfx/image.h"

#include <string.h>

/* "Another way to scroll": a line of text scrolls across the hill picture,
 * wrapped over the slope through the original's precomputed position
 * tables (inverted offline to one lookup per screen pixel). The scene
 * opens with only the green leaves fading in, then the whole picture and
 * the text; it fades out at the end. */

#define SCROLL_MS   (SYNC_FOREST_SCROLL - SYNC_FOREST_START)
#define FADEOUT_MS  (SYNC_FOREST_FADEOUT - SYNC_FOREST_START)
#define FOREST_MS   (FADEOUT_MS + 64 * 1000 / 70)
#define TEXT_START  104 /* window column showing text column 0 at first */
#ifndef LEAF_GAIN
#define LEAF_GAIN 24 /* leaf brightness, 16 = as the original */
#endif

enum {
    BgFull,
    BgLeaves,
    TextFull,
    TextLeaves,
};

typedef struct {
    Image pics; /* 4 frames, see enum */
    uint8_t* blob;
    uint16_t win_w, win_h, text_w, text_h;
    const uint8_t* map; /* 85x64 u16 window pixel or 0xffff */
    const uint8_t* text; /* text_h x text_w levels 0..7, 2 per byte */
} Forest;

static void* forest_init(const Scene* self) {
    (void)self;
    Forest* s = plat_alloc(sizeof(Forest));
    if(!s) return NULL;
    size_t size = 0;
    s->blob = plat_load_asset("forest.bin", &size);
    if(!s->blob || size < 12 || memcmp(s->blob, "SRF2", 4) || !image_load(&s->pics, "forest.srg") ||
       s->pics.frames < 4) {
        image_free(&s->pics);
        plat_free(s->blob);
        plat_free(s);
        return NULL;
    }
    const uint8_t* d = s->blob;
    s->win_w = d[4] | d[5] << 8;
    s->win_h = d[6] | d[7] << 8;
    s->text_w = d[8] | d[9] << 8;
    s->text_h = d[10] | d[11] << 8;
    s->map = d + 12;
    s->text = s->map + s->pics.w * s->pics.h * 2;

    /* The leaves are very dark (12% on average) and get lost on the LCD:
     * brighten them, in the leaves-only picture and by the same amount in
     * the full one (where they're the same pixels, mixed at the edges). */
    uint8_t* full = (uint8_t*)image_frame(&s->pics, BgFull);
    uint8_t* leaves = (uint8_t*)image_frame(&s->pics, BgLeaves);
    for(int i = 0; i < s->pics.w * s->pics.h; i++) {
        int add = leaves[i] * (LEAF_GAIN - 16) / 16;
        leaves[i] = (uint8_t)clampi(leaves[i] + add, 0, 255);
        full[i] = (uint8_t)clampi(full[i] + add, 0, 255);
    }
    return s;
}

static void forest_render(void* state, Frame* f, uint32_t t_ms) {
    Forest* s = state;
    const Rect v = VIEW_43;
    int32_t tick = (int32_t)frame70(t_ms);

    /* leaves-only -> full palette mix, overall brightness */
    int mix = 0, level = 256;
    if(t_ms < SCROLL_MS) {
        level = clampi(tick * 256 / 63, 0, 256);
    } else {
        mix = clampi((int32_t)frame70(t_ms - SCROLL_MS) * 256 / 128, 0, 256);
        if(t_ms >= FADEOUT_MS) level = clampi(256 - (int32_t)frame70(t_ms - FADEOUT_MS) * 256 / 63, 0, 256);
    }
    int32_t step = t_ms < SCROLL_MS ? 0 : (int32_t)frame70(t_ms - SCROLL_MS) / 3;

    const uint8_t* full = image_frame(&s->pics, BgFull);
    const uint8_t* leaves = image_frame(&s->pics, BgLeaves);
    const uint8_t* tfull = image_frame(&s->pics, TextFull);
    const uint8_t* tleaves = image_frame(&s->pics, TextLeaves);

    gfx_clear(f, 0);
    for(int y = 0; y < v.h; y++) {
        for(int x = 0; x < v.w; x++) {
            int i = y * v.w + x;
            int bg = leaves[i] + ((full[i] - leaves[i]) * mix >> 8);
            int px = bg;
            uint16_t k = s->map[i * 2] | s->map[i * 2 + 1] << 8;
            if(k != 0xffff) {
                int r = k / s->win_w, c = k % s->win_w;
                int col = c - TEXT_START + step;
                if(col > s->text_w - 1) col = s->text_w - 1;
                if(r < s->text_h && col >= 0) {
                    int t = (s->text[(r * s->text_w + col) >> 1] >> ((col & 1) * 4)) & 15;
                    if(t) {
                        int on = tleaves[i] + ((tfull[i] - tleaves[i]) * mix >> 8);
                        /* the original's highlight is subtle; lift it so
                         * the text reads on the LCD */
                        int lift = (bg + 110) * mix >> 8;
                        if(on < lift) on = lift > 255 ? 255 : lift;
                        px = bg + (on - bg) * t / 7;
                    }
                }
            }
            f->px[v.y + y][v.x + x] = (uint8_t)clampi(px * level >> 8, 0, 255);
        }
    }
}

static void forest_deinit(void* state) {
    Forest* s = state;
    image_free(&s->pics);
    plat_free(s->blob);
    plat_free(s);
}

const Scene scene_forest = {
    .name = "Forest",
    .duration_ms = FOREST_MS,
    .init = forest_init,
    .render = forest_render,
    .deinit = forest_deinit,
};
