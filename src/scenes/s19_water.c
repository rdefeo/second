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
#define WIN_W      158 /* the original window's width */

typedef struct {
    Image bg;
    uint8_t* hits; /* n x (x, y, window row, column) */
    uint8_t* sword; /* brightness, 0 = transparent */
    uint16_t n, sword_w, sword_h;
} Water;

static void water_deinit(void* state) {
    Water* s = state;
    image_free(&s->bg);
    plat_free(s->hits);
    plat_free(s->sword);
    plat_free(s);
}

/* The hit list and the sword go in separate blocks: smaller holes are
 * easier to find in the heap. */
static bool water_load(Water* s) {
    if(!image_load(&s->bg, "water.srg")) return false;
    PlatFile* file = plat_asset_open("water.bin");
    if(!file) return false;
    uint8_t hdr[12];
    bool ok = plat_asset_read(file, 0, hdr, sizeof(hdr)) && !memcmp(hdr, "SRW2", 4);
    if(ok) {
        s->n = hdr[4] | hdr[5] << 8;
        s->sword_w = hdr[6] | hdr[7] << 8;
        s->sword_h = hdr[8] | hdr[9] << 8;
        size_t hits = (size_t)s->n * 4, sword = (size_t)s->sword_w * s->sword_h;
        ok = s->sword_w > LAST_COL && (s->hits = plat_alloc(hits)) &&
             plat_asset_read(file, sizeof(hdr), s->hits, hits) && (s->sword = plat_alloc(sword)) &&
             plat_asset_read(file, sizeof(hdr) + hits, s->sword, sword);
    }
    plat_asset_close(file);
    return ok;
}

static void* water_init(const Scene* self) {
    (void)self;
    Water* s = plat_alloc(sizeof(Water));
    if(!s) return NULL;
    if(!water_load(s)) {
        water_deinit(s);
        return NULL;
    }
    return s;
}

/* lift the dark scene's mid-tones */
static inline int water_lift(int px, int level) {
    return px * (510 - px) / 255 * level >> 8;
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
    for(int y = 0; y < v.h; y++)
        for(int x = 0; x < v.w; x++)
            f->px[v.y + y][v.x + x] = (uint8_t)water_lift(bg[y * v.w + x], level);
    if(step <= 0) return;

    for(int k = 0; k < s->n; k++) {
        const uint8_t* h = s->hits + k * 4;
        int col = h[3] - WIN_W + step;
        if(col > LAST_COL) col = LAST_COL;
        if(col < 0) continue;
        int sv = s->sword[h[2] * s->sword_w + col];
        if(sv) f->px[v.y + h[1]][v.x + h[0]] = (uint8_t)water_lift(sv, level);
    }
}

const Scene scene_water = {
    .name = "Water",
    .duration_ms = WATER_MS,
    .init = water_init,
    .render = water_render,
    .deinit = water_deinit,
};
