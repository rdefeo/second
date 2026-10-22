#include "scene.h"
#include "../timeline.h"

#include <string.h>

/* Bouncing dots: 512 particles fall onto a floor (with shadows) and bounce.
 * A new particle is (re)emitted every tick, so the shapes change as the
 * emitter changes: a swirling drop, a fountain ring, a pulsing ring, then
 * random rain while the camera starts spinning, a bluish flash and a fade.
 * The simulation is stepped at 70 Hz; rewinding replays from the start. */

#define NDOTS      512
#define FADE_TICKS 128
#define LAST_TICK  2450
#define DOTS_MS    (SYNC_DOTS_END - SYNC_PLZ_CUBE_END)

typedef struct {
    float x, y, z, vy;
} Dot;

typedef struct {
    Dot dot[NDOTS];
    int32_t tick; /* simulation ticks done (after the fade-in) */
    int32_t emit_t; /* emitter clock, reset when the fountain starts */
    int32_t next;
    float spin, spin_v, sway;
    int32_t dropper;
    int32_t gravity;
    uint32_t rng;
    float rot_sin, rot_cos;
    uint8_t floor_lum[FB_H];
} Dots;

/* sin/cos with 1024 steps per turn, amplitude 255 */
static float isin(float a) {
    return fx_sin((int32_t)a) * (255.0f / FX_ONE14);
}

static float icos(float a) {
    return fx_cos((int32_t)a) * (255.0f / FX_ONE14);
}

static int32_t rnd15(Dots* d) {
    d->rng = d->rng * 1103515245u + 12345u;
    return (int32_t)((d->rng >> 16) & 0x7FFF);
}

static void dots_reset(Dots* d) {
    memset(d->dot, 0, sizeof(d->dot));
    d->tick = 0;
    d->emit_t = 0;
    d->next = 0;
    d->spin = d->sway = 0;
    d->spin_v = -64;
    d->dropper = 22000;
    d->gravity = 3;
    d->rng = 0x5EC0D;
    for(int i = 0; i < NDOTS; i++)
        d->dot[i].y = 2560 - d->dropper;
}

/* where a dot lands on screen, in original 320x200 coordinates */
static void project(const Dots* d, const Dot* p, float* depth, float* sx) {
    *depth = (p->z * d->rot_cos - p->x * d->rot_sin) / 65536.0f + 9000.0f;
    float a = (p->z * d->rot_sin + p->x * d->rot_cos) / 256.0f;
    *sx = (a + a / 8) / *depth + 160.0f;
}

static void dots_step(Dots* d) {
    int32_t t = ++d->tick;
    if(t == 500) d->emit_t = 0;
    int32_t e = d->emit_t;
    d->next = (d->next + 1) % NDOTS;
    Dot* n = &d->dot[d->next];

    if(t < 500) { /* swirling column dropped from above */
        *n = (Dot){isin(e * 11) * 40, icos(e * 13) * 10 - d->dropper, isin(e * 17) * 40, 0};
    } else if(t < 900) { /* fountain ring */
        *n = (Dot){icos(e * 15) * 55, (float)d->dropper, isin(e * 15) * 55, -260};
    } else if(t < 1700) { /* ring whose size pulses */
        float r = (float)(int32_t)(isin(t) / 8);
        *n = (Dot){icos(e * 66) * r, 8000, isin(e * 66) * r, -300};
    } else if(t < 2360) { /* random rain, gravity slowly weakening */
        *n = (Dot){
            (float)(rnd15(d) - 16384), 8000.0f - rnd15(d) / 2, (float)(rnd15(d) - 16384), 0};
        if(t > 1900 && !(t & 31) && d->gravity > 0) d->gravity--;
    }

    if(d->dropper > 4000) d->dropper -= 100;
    d->rot_cos = icos(d->spin) * 64;
    d->rot_sin = isin(d->spin) * 64;
    d->sway += 2;
    if(t > 1900) {
        d->spin += d->spin_v / 64;
        d->spin_v -= 1;
    } else {
        d->spin = isin(d->sway);
    }
    d->emit_t++;

    for(int i = 0; i < NDOTS; i++) {
        Dot* p = &d->dot[i];
        float depth, sx;
        project(d, p, &depth, &sx);
        if(sx > 319 || 524288.0f / depth + 100 > 199) continue; /* off screen: frozen */
        p->vy += d->gravity;
        float y = p->y + p->vy;
        if(y >= 8105) { /* floor: bounce, losing some energy */
            p->vy = -p->vy * 13 / 16;
            y += p->vy;
        }
        p->y = y;
    }
}

static void* dots_init(const Scene* self) {
    (void)self;
    Dots* d = plat_alloc(sizeof(Dots));
    if(!d) return NULL;
    dots_reset(d);
    /* floor gradient on the lower half (lifted a bit for the LCD) */
    for(int y = 0; y < FB_H; y++) {
        int a = (int)((y - FB_H / 2 + 0.5f) / ORIG_SY);
        if(a < 0) continue;
        float c = 64.0f - 256.0f / (a + 4);
        d->floor_lum[y] = vga_lum((int)(c * c / 64 / 4 * 1.6f), (int)(c * c / 64 / 4 * 1.6f), (int)(c * c / 64 / 4 * 1.6f));
    }
    return d;
}

static void dots_render(void* state, Frame* f, uint32_t t_ms) {
    Dots* d = state;
    int32_t tick = (int32_t)frame70(t_ms);
    int32_t sim = tick - FADE_TICKS;
    if(sim > LAST_TICK) sim = LAST_TICK;
    if(sim < d->tick) dots_reset(d);
    while(d->tick < sim)
        dots_step(d);

    for(int y = 0; y < FB_H; y++)
        memset(f->px[y], d->floor_lum[y], FB_W);

    for(int i = 0; i < NDOTS; i++) {
        const Dot* p = &d->dot[i];
        float depth, sx;
        project(d, p, &depth, &sx);
        if(sx < 0 || sx > 319) continue;
        int x = (int)(sx * ORIG_SX);
        float shadow = 524288.0f / depth + 100;
        if(shadow <= 199) gfx_pset(f, x, (int)(shadow * ORIG_SY), 0);
        float sy = p->y * 64 / depth + 100;
        if(sy < 0 || sy > 199) continue;
        /* nearer dots are brighter. The original's are cyan, which comes
         * out under 2/3 grey and gets lost on the floor: use brightness
         * only, far dots at 70%, near ones white, the right pixel dimmer */
        int c = clampi((((int32_t)depth >> 8) - 31) * 3 / 4 + 8, 0, 15);
        int k = 180 + (15 - c) * 5;
        int y = (int)(sy * ORIG_SY);
        gfx_pset(f, x, y, (uint8_t)k);
        gfx_pset(f, x + 1, y, (uint8_t)(k * 3 / 4));
    }

    /* fade in from black, bluish flash, fade out */
    int32_t t = sim;
    if(tick < FADE_TICKS) {
        int k = tick * 256 / FADE_TICKS;
        for(int y = 0; y < FB_H; y++)
            for(int x = 0; x < FB_W; x++)
                f->px[y][x] = (uint8_t)(f->px[y][x] * k >> 8);
    } else if(t >= 2360 && t < 2400) {
        int add = (t - 2360) * 3 * 4; /* +3 per tick on a 0..63 scale */
        for(int y = 0; y < FB_H; y++)
            for(int x = 0; x < FB_W; x++)
                f->px[y][x] = (uint8_t)clampi(f->px[y][x] + add, 0, 255);
    } else if(t >= 2400) {
        gfx_clear(f, (uint8_t)(clampi(63 - (t - 2400) * 2, 0, 63) * 255 / 63));
    }
}

static void dots_deinit(void* state) {
    plat_free(state);
}

const Scene scene_dots = {
    .name = "Dots",
    .duration_ms = DOTS_MS,
    .init = dots_init,
    .render = dots_render,
    .deinit = dots_deinit,
};
