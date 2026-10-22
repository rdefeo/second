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

typedef struct {
    Image bg; /* 2 frames: full palette, leaves only */
    uint8_t* hits; /* n x (x, y, window row, column, text full, leaves) */
    uint8_t* text; /* text_h x text_w levels 0..7, 2 per byte */
    uint16_t n, text_w, text_h;
} Forest;

static void forest_deinit(void* state) {
    Forest* s = state;
    image_free(&s->bg);
    plat_free(s->hits);
    plat_free(s->text);
    plat_free(s);
}

/* The hit list and the text go in separate blocks: two ~10 KB holes are
 * easier to find in the heap than one 20 KB one. */
static bool forest_load(Forest* s) {
    if(!image_load(&s->bg, "forest.srg") || s->bg.frames < 2) return false;
    PlatFile* file = plat_asset_open("forest.bin");
    if(!file) return false;
    uint8_t hdr[12];
    bool ok = plat_asset_read(file, 0, hdr, sizeof(hdr)) && !memcmp(hdr, "SRF3", 4);
    if(ok) {
        s->n = hdr[4] | hdr[5] << 8;
        s->text_w = hdr[6] | hdr[7] << 8;
        s->text_h = hdr[8] | hdr[9] << 8;
        size_t hits = (size_t)s->n * 6, text = (size_t)s->text_w * s->text_h / 2;
        ok = (s->hits = plat_alloc(hits)) && plat_asset_read(file, sizeof(hdr), s->hits, hits) &&
             (s->text = plat_alloc(text)) && plat_asset_read(file, sizeof(hdr) + hits, s->text, text);
    }
    plat_asset_close(file);
    return ok;
}

static void* forest_init(const Scene* self) {
    (void)self;
    Forest* s = plat_alloc(sizeof(Forest));
    if(!s) return NULL;
    if(!forest_load(s)) {
        forest_deinit(s);
        return NULL;
    }

    /* The leaves are very dark (12% on average) and get lost on the LCD:
     * brighten them, in the leaves-only picture and by the same amount in
     * the full one (where they're the same pixels, mixed at the edges). */
    uint8_t* full = (uint8_t*)image_frame(&s->bg, 0);
    uint8_t* leaves = (uint8_t*)image_frame(&s->bg, 1);
    for(int i = 0; i < s->bg.w * s->bg.h; i++) {
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

    const uint8_t* full = image_frame(&s->bg, 0);
    const uint8_t* leaves = image_frame(&s->bg, 1);

    gfx_clear(f, 0);
    for(int y = 0; y < v.h; y++) {
        for(int x = 0; x < v.w; x++) {
            int i = y * v.w + x;
            int bg = leaves[i] + ((full[i] - leaves[i]) * mix >> 8);
            f->px[v.y + y][v.x + x] = (uint8_t)clampi(bg * level >> 8, 0, 255);
        }
    }

    /* the text, at the screen pixels the window maps to */
    for(int k = 0; k < s->n; k++) {
        const uint8_t* h = s->hits + k * 6;
        int col = h[3] - TEXT_START + step;
        if(col > s->text_w - 1) col = s->text_w - 1;
        if(col < 0) continue;
        int t = (s->text[(h[2] * s->text_w + col) >> 1] >> ((col & 1) * 4)) & 15;
        if(!t) continue;
        int i = h[1] * v.w + h[0];
        int bg = leaves[i] + ((full[i] - leaves[i]) * mix >> 8);
        int on = h[5] + ((h[4] - h[5]) * mix >> 8);
        /* the original's highlight is subtle; lift it so the text reads
         * on the LCD */
        int lift = (bg + 110) * mix >> 8;
        if(on < lift) on = lift > 255 ? 255 : lift;
        int px = bg + (on - bg) * t / 7;
        f->px[v.y + h[1]][v.x + h[0]] = (uint8_t)clampi(px * level >> 8, 0, 255);
    }
}

const Scene scene_forest = {
    .name = "Forest",
    .duration_ms = FOREST_MS,
    .init = forest_init,
    .render = forest_render,
    .deinit = forest_deinit,
};
