#include "scene.h"
#include "../timeline.h"

/* Plasma: two independent sum-of-sines fields, interleaved in a
 * checkerboard so they blend into a smoky texture, looked up through a
 * cosine-ramp palette. Three palette sequences; at each music sync the
 * plasma "drops" off the bottom of the screen and comes back with the next
 * palette, fading in from black (the first one fades in from white). */

#define TAB    4096
#define TABM   (TAB - 1)
#define DROP_T 64 /* drop length in 70 Hz ticks */

typedef struct {
    int16_t wave_a[TAB]; /* distorts the first field's x */
    int16_t wave_b[TAB]; /* distorts the second field's x */
    int16_t field[TAB];
    uint8_t lum[3][256]; /* palette brightness per sequence */
} Plasma;

/* Drop syncs, and the sequence that follows each drop, ms from PLZ_START. */
#define DROP_MS(n) (SYNC_PLZ_DROP##n - SYNC_PLZ_START)
#define DROP_LEN   (DROP_T * 1000 / 70)
static const uint32_t seq_start_ms[3] = {0, DROP_MS(1) + DROP_LEN, DROP_MS(2) + DROP_LEN};
static const uint32_t drop_ms[3] = {DROP_MS(1), DROP_MS(2), DROP_MS(3)};
#define PLASMA_MS (DROP_MS(3) + DROP_LEN)

/* Starting phases of the eight wave offsets for each sequence:
 * {a0,a1,a2,a3} for one field, {b0,b1,b2,b3} for the other. */
static const int16_t phases[3][8] = {
    {3500, 2300, 3900, 3670, 1000, 2000, 3000, 4000},
    {1500, 2300, 3900, 1670, 1000, 2000, 4000, 4000},
    {3500, 3300, 2900, 2670, 3500, 1000, 3000, 1000},
};
/* Per-tick drift of those offsets. */
static const int8_t drift[8] = {-3, -2, 1, 2, -1, -2, 2, 3};

/* sin(2*pi*k*a/4096) in Q14 using the 1024-step table */
static int32_t s4096(int32_t a, int32_t k) {
    return fx_sin((a * k) >> 2);
}

/* Palette channel shapes for one 64-entry segment. */
enum {
    Z, /* off */
    U, /* ramps up */
    D, /* ramps down */
    F, /* full */
};

static int ramp(int a) {
    /* smooth 0..63 ease: 32 - 31*cos(pi*a/64) */
    if(a <= 0) return 0;
    return 32 - (31 * fx_cos(a * 8)) / FX_ONE14;
}

static int shape(int kind, int a) {
    switch(kind) {
    case U:
        return ramp(a);
    case D:
        return ramp(63 - a);
    case F:
        return ramp(63);
    default:
        return 0;
    }
}

/* [sequence][segment][r,g,b] */
static const uint8_t pal_shapes[3][4][3] = {
    {{U, Z, Z}, {D, Z, Z}, {Z, Z, U}, {U, Z, D}}, /* red / blue */
    {{U, Z, Z}, {D, Z, U}, {Z, U, D}, {U, F, U}}, /* red / blue / green */
    {{Z, Z, Z}, {U, U, U}, {D, D, D}, {Z, Z, Z}}, /* grey */
};

static void* plasma_init(const Scene* self) {
    (void)self;
    Plasma* p = plat_alloc(sizeof(Plasma));
    if(!p) return NULL;
    for(int32_t a = 0; a < TAB; a++) {
        p->wave_a[a] = (int16_t)(((55 * s4096(a, 1) + 5 * s4096(a, 4) + 3 * s4096(a, 17)) / FX_ONE14 + 64) * 16);
        p->wave_b[a] = (int16_t)(((55 * s4096(a, 1) + 8 * s4096(a, 5) + 2 * s4096(a, 15)) / FX_ONE14 + 64) * 8);
        p->field[a] = (int16_t)((55 * s4096(a, 1) + 5 * s4096(a, 6) + 4 * s4096(a, 21)) / FX_ONE14 + 64);
    }
    for(int s = 0; s < 3; s++) {
        int peak = 1;
        int raw[256];
        for(int i = 0; i < 256; i++) {
            int seg = i >> 6, a = i & 63;
            const uint8_t* sh = pal_shapes[s][seg];
            raw[i] = i == 0 ? 0 : vga_lum(shape(sh[0], a), shape(sh[1], a), shape(sh[2], a));
            if(raw[i] > peak) peak = raw[i];
        }
        for(int i = 0; i < 256; i++)
            p->lum[s][i] = (uint8_t)(raw[i] * 255 / peak);
    }
    return p;
}

static void plasma_render(void* state, Frame* f, uint32_t t_ms) {
    Plasma* p = state;

    int seq = 0;
    while(seq < 2 && t_ms >= seq_start_ms[seq + 1])
        seq++;
    int32_t tick = (int32_t)frame70(t_ms - seq_start_ms[seq]);

    int32_t ofs[8];
    for(int i = 0; i < 8; i++)
        ofs[i] = (phases[seq][i] + drift[i] * tick) & TABM;

    /* brightness ramp: from white at the very start, from black afterwards */
    int fade_in = seq == 0 ? 127 : 32;
    int k = tick >= fade_in ? 256 : tick * 256 / fade_in;
    uint8_t lut[256];
    for(int i = 0; i < 256; i++) {
        int v = p->lum[seq][i];
        lut[i] = (uint8_t)(seq == 0 ? 255 - ((255 - v) * k >> 8) : (v * k) >> 8);
    }

    /* during a drop the plasma accelerates down and off the screen */
    int drop = 0;
    if(t_ms >= drop_ms[seq]) {
        int32_t d = (int32_t)frame70(t_ms - drop_ms[seq]) + 1;
        if(d > DROP_T) d = DROP_T;
        drop = (d * d / 4 * 43 / 128) * FB_H / 280; /* original lines -> rows */
    }
    gfx_fill_rect(f, 0, 0, FB_W, drop, 0);

    for(int y = drop; y < FB_H; y++) {
        int32_t oy = (y - drop) * 35 / 8; /* 280 original lines */
        for(int x = 0; x < FB_W; x++) {
            int32_t ox = x * 5 / 8; /* 80 original columns */
            int32_t rx = 80 - ox;
            const int32_t* o = ((x ^ y) & 1) ? ofs + 4 : ofs;
            int32_t v1 = p->field[(ox * 8 + o[0] + p->wave_a[(oy + o[1] + rx * 4) & TABM]) & TABM];
            int32_t v2 = p->field[(p->wave_b[(oy + o[3] + ox * 16) & TABM] + oy * 2 + o[2] + rx * 4) & TABM];
            f->px[y][x] = lut[(v1 + v2) & 255];
        }
    }
}

static void plasma_deinit(void* state) {
    plat_free(state);
}

const Scene scene_plasma = {
    .name = "Plasma",
    .duration_ms = PLASMA_MS,
    .init = plasma_init,
    .render = plasma_render,
    .deinit = plasma_deinit,
};
