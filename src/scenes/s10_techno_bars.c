#include "scene.h"

#include <string.h>

/* Techno bars: a set of 11 parallel bars rotates around the screen while
 * the gap between them breathes. The original rendered each tick's bars
 * into one bitplane of one of 8 video pages, so the page on screen always
 * holds the bars from 4 ticks spaced 8 apart, and brightness = how many of
 * those 4 bar sets cover a pixel. We redraw those 4 sets each frame.
 *
 * Three sequences: steady spin; accelerating spin; accelerating spin with
 * the whole set orbiting, which finally slides off to the right. On every
 * 8th music row the palette flashes brighter. */

#define SEQ1_TICKS 420
#define SEQ2_TICKS 840
#define SLIDE_AT   647 /* tick in sequence 3 when the slide-out starts */
/* Ends so that its last frame is sequence 3's tick 701, the first one
 * the slide-out leaves fully black (the frame at tick t shows the state
 * before that tick's step). Held black, it leads into the troll. */
#define BARS_MS    ((420 + 840 + 702) * 1000 / 70)
#define ROW_MS     57.69f
#define HISTORY    4
#define SPACING    8

typedef struct {
    float rot, rota;
    int32_t vm, vma; /* bar gap with a bouncing "gravity" */
    int32_t rot2; /* orbit angle (sequence 3) */
    int32_t xpos, xposa; /* slide-out (sequence 3) */
} Bars;

static void bars_reset(Bars* b, int seq) {
    b->rot = seq == 1 ? 50.0f : 45.0f;
    b->rota = 10.0f;
    b->vm = seq == 0 ? 50 : 100 * 64;
    b->vma = 0;
    b->rot2 = 0;
    b->xpos = 0;
    b->xposa = 0;
}

static void bars_step(Bars* b, int seq, int32_t tick) {
    if(seq == 0) {
        b->rot += 2.0f;
        b->vm += b->vma;
        if(b->vm < 25) {
            b->vm -= b->vma;
            b->vma = -b->vma;
        }
    } else {
        b->rot += b->rota / 10.0f;
        b->vm += b->vma;
        if(b->vm < 0) {
            b->vm -= b->vma;
            b->vma = -b->vma;
        }
        b->rota += 1.0f;
    }
    b->vma -= 1;
    if(seq == 2) {
        b->rot2 += 17;
        if(tick > SLIDE_AT) {
            b->xpos += b->xposa / 4;
            if(b->xpos > 320)
                b->xpos = 320;
            else
                b->xposa++;
        }
    }
}

static void bars_draw(const Bars* b, int seq, Frame* f, uint8_t bit) {
    float spread = seq == 0 ? b->vm / 100.0f : b->vm / 6400.0f;
    int32_t r = (int32_t)b->rot;
    float s = fx_sin(r) * 255.0f / FX_ONE14, c = fx_cos(r) * 255.0f / FX_ONE14;
    float hx = s * 16 * 6 / 5, hy = c * 16; /* along the bars */
    float vx = c * 6 / 5 * spread, vy = -s * spread; /* across the bars */

    float cx = 160, cy = 100;
    if(seq == 2) {
        /* orbit radius ramps up over the first ticks, then 63.75 px */
        float rad = b->rot2 < 32 ? 255.0f * b->rot2 / 8 : 63.75f;
        if(rad > 63.75f) rad = 63.75f;
        cx += fx_sin(b->rot2) * rad / FX_ONE14;
        cy += fx_cos(b->rot2) * rad / FX_ONE14;
    }

    for(int k = -10; k <= 10; k += 2) {
        float ox = vx * k * 2, oy = vy * k * 2;
        Vec2 q[4] = {
            {(-hx - vx + ox) / 16 + cx, (-hy - vy + oy) / 16 + cy},
            {(-hx + vx + ox) / 16 + cx, (-hy + vy + oy) / 16 + cy},
            {(hx + vx + ox) / 16 + cx, (hy + vy + oy) / 16 + cy},
            {(hx - vx + ox) / 16 + cx, (hy - vy + oy) / 16 + cy},
        };
        for(int i = 0; i < 4; i++) {
            q[i].x = q[i].x * ORIG_SX;
            q[i].y = q[i].y * ORIG_SY;
        }
        gfx_fill_convex(f, q, 4, bit, FillOr);
    }
}

static void* bars_init(const Scene* self) {
    (void)self;
    return plat_alloc(sizeof(Bars));
}

static void bars_render(void* state, Frame* f, uint32_t t_ms) {
    Bars* b = state;
    int32_t tick = (int32_t)frame70(t_ms);
    int seq = 0;
    if(tick >= SEQ1_TICKS + SEQ2_TICKS) {
        seq = 2;
        tick -= SEQ1_TICKS + SEQ2_TICKS;
    } else if(tick >= SEQ1_TICKS) {
        seq = 1;
        tick -= SEQ1_TICKS;
    }

    /* Replay the sequence up to now, drawing the 4 remembered ticks. The
     * slide offset is the one in effect at the current tick. */
    gfx_clear(f, 0);
    bars_reset(b, seq);
    Bars now = *b;
    for(int32_t i = 0; i <= tick; i++) {
        if(i == tick) now = *b;
        bars_step(b, seq, i);
    }
    if(now.xpos >= 320) return;

    bars_reset(b, seq);
    for(int32_t i = 0; i <= tick; i++) {
        int32_t age = tick - i;
        if(age % SPACING == 0 && age / SPACING < HISTORY)
            bars_draw(b, seq, f, (uint8_t)(1 << (age / SPACING)));
        bars_step(b, seq, i);
    }

    /* beat flash: rows 7, 15, 23... after the scene starts at row 32 */
    int32_t row = (int32_t)(t_ms / ROW_MS);
    int32_t flash_row = row - ((row + 1) & 7);
    int32_t boost = 0;
    if(flash_row >= 7) {
        int32_t since = (int32_t)frame70((uint32_t)(t_ms - flash_row * ROW_MS));
        boost = clampi(15 - since, 0, 15);
    }
    static const uint8_t base[5][3] = {
        {0, 0, 0}, {38, 33, 44}, {52, 45, 58}, {67, 61, 73}, {83, 77, 89}};
    uint8_t lut[16];
    for(int i = 0; i < 16; i++) {
        int n = __builtin_popcount(i);
        int r = base[n][0] * 64 / 111 * (10 + boost) / 10;
        int g = base[n][1] * 64 / 111 * (10 + boost * 7 / 9) / 10;
        int bl = base[n][2] * 64 / 111 * (10 + boost * 5 / 9) / 10;
        lut[i] = vga_lum(r, g, bl);
    }
    for(int y = 0; y < FB_H; y++)
        for(int x = 0; x < FB_W; x++)
            f->px[y][x] = lut[f->px[y][x] & 15];

    /* Slide-out: like the original, shift the finished picture to the
     * right and let black come in from the left (shifting the bars
     * themselves would just pull more of their length into view). */
    int dx = clampi((int)(now.xpos * ORIG_SX + 0.5f), 0, FB_W);
    if(dx) {
        for(int y = 0; y < FB_H; y++) {
            memmove(&f->px[y][dx], &f->px[y][0], FB_W - dx);
            memset(&f->px[y][0], 0, dx);
        }
    }
}

static void bars_deinit(void* state) {
    plat_free(state);
}

const Scene scene_techno_bars = {
    .name = "Techno Bars",
    .duration_ms = BARS_MS,
    .init = bars_init,
    .render = bars_render,
    .deinit = bars_deinit,
};
