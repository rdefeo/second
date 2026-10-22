#include "scene.h"
#include "../timeline.h"

/* Techno circles, two phases:
 *  1. A static set of concentric rings (8 ring "colours" repeating outward,
 *     rings getting thinner with distance). Lighting one colour per tick
 *     makes the rings appear to flow; then everything flashes to white.
 *  2. Interference: the ring set and a second set of even-width bands drift
 *     around on circular paths and are combined; the band set also gets a
 *     growing per-line sine wobble, and the palette rotates. */

#define PHASE1_TICKS  256
#define WOBBLE_DELAY  350
/* starts after Glenz (2164 ticks) and the tunnel (1060 ticks) */
#define CIRCLES_MS    (SYNC_TECHNO_CIRCLES2_END - SYNC_GLENZ_START - (2164 + 1060) * 1000 / 70)

/* Rings are drawn in screen pixels, 0.32 screen px per original pixel. */
#define RING_SCALE 0.32f
#define QW         100 /* quadrant table covers |dx| < QW, |dy| < QH */
#define QH         56

typedef struct {
    /* bits 0-2: ring colour of set A, bit 3: band of set B */
    uint8_t quad[QH][QW];
} Circles;

/* Ring k of set A starts at radius 6.5 * k^0.75 original px (measured from
 * the original bitmap); the table is built by walking those thresholds. */
static void* circles_init(const Scene* self) {
    (void)self;
    Circles* c = plat_alloc(sizeof(Circles));
    if(!c) return NULL;

    /* squared radius (in screen px * 16) at which ring k starts; only
     * needed here, so it's not kept (static data stays for the whole app) */
    uint32_t* ring_r2 = plat_alloc(640 * sizeof(uint32_t));
    if(!ring_r2) {
        plat_free(c);
        return NULL;
    }
    for(uint32_t k = 0; k < 640; k++) {
        /* r_k = 6.5 * k^0.75 original px  ->  r_k^2 = 42.25 * k^1.5 */
        uint32_t k15 = k * isqrt32(k * 65536) / 256; /* k^1.5 */
        ring_r2[k] = (uint32_t)(42.25f * k15 * RING_SCALE * RING_SCALE * 256.0f);
    }
    for(int dy = 0; dy < QH; dy++) {
        int k = 0;
        for(int dx = 0; dx < QW; dx++) {
            uint32_t r2 = (uint32_t)(dx * dx + dy * dy) * 256;
            while(k < 639 && ring_r2[k + 1] <= r2)
                k++;
            /* set B: 12 original px wide bands */
            uint32_t r_orig = isqrt32(r2) / 16 * 100 / 32; /* screen -> original px */
            uint8_t band = ((r_orig + 1) / 12) & 1;
            c->quad[dy][dx] = (uint8_t)((k & 7) | (band << 3));
        }
    }
    plat_free(ring_r2);
    return c;
}

static inline uint8_t quad_at(const Circles* c, int dx, int dy) {
    dx = dx < 0 ? -dx : dx;
    dy = dy < 0 ? -dy : dy;
    if(dx >= QW) dx = QW - 1;
    if(dy >= QH) dy = QH - 1;
    return c->quad[dy][dx];
}

/* phase 1 palette: one lit ring colour (0,30,40); after 128 ticks every
 * colour is pushed toward white */
static void phase1(const Circles* c, Frame* f, int32_t tick) {
    int fader = tick * 2 > 512 ? 512 : tick * 2;
    int lit = (tick + 1) & 7;
    uint8_t on, off;
    if(fader <= 256) {
        on = vga_lum(0, 30 * fader / 256, 40 * fader / 256);
        off = 0;
    } else {
        int add = fader - 256;
        on = vga_lum(add, 30 + add, 40 + add);
        off = vga_lum(add, add, add);
    }
    for(int y = 0; y < FB_H; y++)
        for(int x = 0; x < FB_W; x++)
            f->px[y][x] = (quad_at(c, x - FB_W / 2, y - FB_H / 2) & 7) == lit ? on : off;
}

/* phase 2 palettes: brightness steps for ring colours with the band off/on */
static const uint8_t shade_a[8] = {30, 60, 50, 40, 30, 20, 10, 0};
static const uint8_t shade_b[8] = {0, 10, 20, 30, 40, 50, 60, 30};

static void phase2(const Circles* c, Frame* f, int32_t tick) {
    int rot = (7 + tick) & 7;
    uint8_t lut[16];
    for(int i = 0; i < 8; i++) {
        int sa = shade_a[(i + 7 - rot) & 7], sb = shade_b[(i + 7 - rot) & 7];
        lut[i] = vga_lum(sa, sa * 8 / 9, sa);
        lut[i + 8] = vga_lum(sb, sb * 7 / 9, sb);
    }

    /* centres orbit ~63 original px around the middle */
    int32_t ra = (5 * tick) & FX_ANGLE_MASK, rb = (211 + 7 * tick) & FX_ANGLE_MASK;
    float orbit = 63.0f * RING_SCALE / FX_ONE14;
    int ax = FB_W / 2 - (int)(fx_sin(ra) * orbit), ay = FB_H / 2 - (int)(fx_cos(ra) * orbit);
    int bx = FB_W / 2 - (int)(fx_sin(rb) * orbit), by = FB_H / 2 - (int)(fx_cos(rb) * orbit);

    int power = 0;
    if(tick > WOBBLE_DELAY) power = clampi((tick - WOBBLE_DELAY) / 16, 1, 15);
    int32_t wob_rot = 7 * (tick + 1);

    for(int y = 0; y < FB_H; y++) {
        int oy = y * 25 / 8; /* original line */
        /* sideways wobble of set B: up to +-31 original px * power/15 */
        int32_t s = fx_sin(wob_rot + 9 * oy) * 31 / FX_ONE14;
        int shift = (int)(s * power / 15 * RING_SCALE);
        for(int x = 0; x < FB_W; x++) {
            uint8_t a = quad_at(c, x - ax, y - ay) & 7;
            uint8_t b = quad_at(c, x - bx + shift, y - by) & 8;
            f->px[y][x] = lut[a | b];
        }
    }
}

static void circles_render(void* state, Frame* f, uint32_t t_ms) {
    int32_t tick = (int32_t)frame70(t_ms);
    if(tick < PHASE1_TICKS)
        phase1(state, f, tick);
    else
        phase2(state, f, tick - PHASE1_TICKS);
}

static void circles_deinit(void* state) {
    plat_free(state);
}

const Scene scene_techno_circles = {
    .name = "Techno Circles",
    .duration_ms = CIRCLES_MS,
    .init = circles_init,
    .render = circles_render,
    .deinit = circles_deinit,
};
