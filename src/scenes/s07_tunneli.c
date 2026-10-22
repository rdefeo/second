#include "scene.h"

/* Dot tunnel. Every 70 Hz tick a new ring of 64 dots is born far away at a
 * point on a growing sine path; older rings move closer (bigger radius).
 * The view is centred on a ring a few slots from the camera, so the tunnel
 * bends as the path wobbles. Every ring's position only depends on its
 * birth tick, so the whole tunnel is a function of time. */

#define TICKS        1060 /* length of the part in 70 Hz ticks */
#define SLOTS        100 /* rings alive at once; slot 99 is the newest */
#define FAR_SLOT     80
#define NEAR_SLOT    4
#define CAMERA_SLOT  5
#define DOTS_PER_RNG 64
#define FADE_TAIL    102 /* last rings are born invisible so the tunnel drains */

typedef struct {
    float x, y;
} Pt;

/* sin(pi*m/128) scaled by a ramp: the wobble grows as the part goes on. */
static float wave_sin(int32_t k) {
    int32_t m = k & 4095;
    return fx_sin(m * 4) * (3.0f * m / 128.0f) / FX_ONE14;
}

static float wave_cos(int32_t k) {
    int32_t m = k & 2047;
    return fx_cos(m * 4) * (4.0f * m / 64.0f) / FX_ONE14;
}

static Pt ring_origin(int32_t born) {
    Pt p = {-wave_sin(born * 3), wave_sin(born * 2) - wave_cos(born) + wave_sin(born)};
    return p;
}

static void* tunneli_init(const Scene* self) {
    (void)self;
    return (void*)1;
}

static void tunneli_render(void* state, Frame* f, uint32_t t_ms) {
    (void)state;
    gfx_clear(f, 0);
    int32_t now = (int32_t)frame70(t_ms);
    if(now > TICKS) now = TICKS;

    /* slot s holds the ring born (SLOTS - 1 - s) ticks ago */
    Pt cam = ring_origin(now - (SLOTS - 1 - CAMERA_SLOT));

    for(int s = FAR_SLOT; s >= NEAR_SLOT; s--) {
        int32_t born = now - (SLOTS - 1 - s);
        if(born < 0 || born >= TICKS - FADE_TAIL) continue;
        /* At 128x64 every ring would merge into a solid halo: keep half of
         * them, and fewer dots on the small far ones. */
        if(born & 1) continue;
        int step = s > 40 ? 2 : 1;

        Pt o = ring_origin(born);
        float bx = o.x - cam.x, by = o.y - cam.y;
        float r = 16384.0f / (s * 7 + 95);

        /* far rings fade out; rings alternate bright/dim in groups of 8.
         * The original fades linearly with depth. On the LCD a lone pixel
         * below full white blinks and looks much dimmer than its value, so
         * the near half of the tunnel is full white and only the far half
         * fades (square-root curve); near dots are also two pixels wide. */
        int level = 64 - (int)(s / 1.3f);
        int v = level >= 32 ? 255 : (int)isqrt32((uint32_t)(level * 255 * 255 / 32));
        if((born & 15) > 7) v = v * 7 / 8;
        int wide = s < 40;

        for(int a = 0; a < DOTS_PER_RNG; a += step) {
            float ox = 160.0f + bx + fx_sin(a * 16) * 1.7f * r / FX_ONE14;
            float oy = 100.0f + by + fx_cos(a * 16) * r / FX_ONE14;
            int px = (int)(ox * ORIG_SX), py = (int)(oy * ORIG_SY);
            gfx_pset(f, px, py, (uint8_t)v);
            if(wide) gfx_pset(f, px + 1, py, (uint8_t)v);
        }
    }
}

static void tunneli_deinit(void* state) {
    (void)state;
}

const Scene scene_tunneli = {
    .name = "Tunneli",
    .duration_ms = 15200,
    .init = tunneli_init,
    .render = tunneli_render,
    .deinit = tunneli_deinit,
};
