#include "scene.h"
#include "../gfx/image.h"

/* Rotozoomer over a tiling texture cut from the face picture. One corner
 * of the source rectangle circles around while the rotation speeds up and
 * the zoom dives in, holds, and finally pulls far out. Fades in from white
 * and out to white. Parameters are stepped at 70 Hz like the original. */

#define ROTO_TICKS 2000
#define TWO_PI     6.28318531f

typedef struct {
    Image tex; /* 128x128 = the original 256x256 at half resolution */
    int32_t tick;
    float d1, d2, d3, scale, scale_v;
} Roto;

static float fsin(float rad) {
    return fx_sin((int32_t)(rad * (FX_ANGLE_STEPS / TWO_PI))) / (float)FX_ONE14;
}

static float fcos(float rad) {
    return fx_cos((int32_t)(rad * (FX_ANGLE_STEPS / TWO_PI))) / (float)FX_ONE14;
}

static void roto_reset(Roto* r) {
    r->tick = -1;
    r->d1 = 0;
    r->d2 = 0.00007654321f;
    r->d3 = 0;
    r->scale = 2;
    r->scale_v = -0.01f;
}

static void roto_step(Roto* r) {
    int32_t f = ++r->tick;
    r->d1 -= .005f;
    r->d2 += r->d3;
    r->scale += r->scale_v;
    if(f > 25 && r->d3 < .02f) r->d3 += 0.00005f; /* spin up */
    if(f < 270) { /* dive in, braking near 0.9 */
        if(r->scale < .9f && r->scale_v < 1) r->scale_v += 0.0001f;
    } else if(f < 400) {
        if(r->scale_v > 0.001f) r->scale_v -= 0.0001f;
    } else if(f > 1600) { /* pull back out */
        if(r->scale_v > -.1f) r->scale_v -= 0.001f;
    } else if(f > 1100) {
        int a = f - 900 > 100 ? 100 : f - 900;
        r->scale_v += 0.000001f * a;
    }
}

static void* roto_init(const Scene* self) {
    (void)self;
    Roto* r = plat_alloc(sizeof(Roto));
    if(!r) return NULL;
    if(!image_load(&r->tex, "roto.srg")) {
        plat_free(r);
        return NULL;
    }
    roto_reset(r);
    return r;
}

static void roto_render(void* state, Frame* f, uint32_t t_ms) {
    Roto* r = state;
    int32_t tick = (int32_t)frame70(t_ms);
    if(tick >= ROTO_TICKS) tick = ROTO_TICKS - 1;
    if(tick < r->tick) roto_reset(r);
    while(r->tick < tick)
        roto_step(r);

    /* step vectors in source texels (256 space) per original pixel */
    float xa = -1024.0f * fsin(r->d2) * r->scale;
    float ya = 1024.0f * fcos(r->d2) * r->scale;
    float u0 = 70.0f * fsin(r->d1) - 30 - xa / 16;
    float v0 = 70.0f * fcos(r->d1) + 60 - ya / 16;
    float pu = ya / 1024, pv = -xa / 1024; /* along a line */
    float lu = -pv * 1.2f, lv = pu * 1.2f; /* down the screen (VGA pixel aspect) */

    /* The original screen is 160x100 (4:3). Sample it with the same pixel
     * proportions as the 4:3 pictures before it (85 columns across the
     * original width), centred: the full 128 columns then simply show more
     * of the tiling texture instead of stretching it.
     * 16.16 fixed point in texel/2 units (texture is half size). */
    const float sx = 160.0f / VIEW_43.w, sy = 100.0f / FB_H;
    const float x0 = 80.0f - (FB_W / 2) * sx; /* original column at screen x 0 */
    int32_t du = (int32_t)(pu * sx * 32768), dv = (int32_t)(pv * sx * 32768);
    const uint8_t* tex = image_frame(&r->tex, 0);
    for(int y = 0; y < FB_H; y++) {
        float ly = (y + 1) * sy;
        int32_t u = (int32_t)((u0 + lu * ly + pu * (x0 + sx)) * 32768);
        int32_t v = (int32_t)((v0 + lv * ly + pv * (x0 + sx)) * 32768);
        uint8_t* row = f->px[y];
        for(int x = 0; x < FB_W; x++) {
            row[x] = tex[((v >> 16) & 127) << 7 | ((u >> 16) & 127)];
            u += du;
            v += dv;
        }
    }

    int k = 0;
    if(tick > ROTO_TICKS - 128)
        k = (tick - (ROTO_TICKS - 128)) * 256 / 128;
    else if(tick < 16)
        k = 256 - tick * 256 / 15;
    if(k > 0) {
        uint8_t lut[256];
        lut_fade_white(lut, clampi(k, 0, 256));
        for(int y = 0; y < FB_H; y++)
            for(int x = 0; x < FB_W; x++)
                f->px[y][x] = lut[f->px[y][x]];
    }
}

static void roto_deinit(void* state) {
    Roto* r = state;
    image_free(&r->tex);
    plat_free(r);
}

const Scene scene_roto = {
    .name = "Rotozoom",
    .duration_ms = ROTO_TICKS * 1000 / 70,
    .init = roto_init,
    .render = roto_render,
    .deinit = roto_deinit,
};
