#include "scene.h"
#include "../gfx/image.h"

/* Explosion: a pre-rendered 41-frame animation played at 17.5 fps, with a
 * white flash at the start and a fade to white at the end. The frames are
 * too big for RAM together, so each one is read from the SD card when
 * needed. */

#define PAM_FRAMES 41
#define PAM_TICKS  55 /* at 70/4 Hz */
#define PAM_MS     (PAM_TICKS * 4000 / 70)

/* white level (0..63) per tick: flash decays, picture, then whiteout */
static const uint8_t whiteness[PAM_TICKS] = {
    63, 32, 16, 8, 4, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0,  0,  0,  0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 4, 6, 9, 14, 20, 28,
    37, 46, 56, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63};

typedef struct {
    PlatFile* file;
    uint16_t w, h;
    int loaded;
    uint8_t* px;
} Pam;

static void* pam_init(const Scene* self) {
    (void)self;
    Pam* p = plat_alloc(sizeof(Pam));
    if(!p) return NULL;
    uint8_t hdr[12];
    p->file = plat_asset_open("pam.srg");
    if(!p->file || !plat_asset_read(p->file, 0, hdr, sizeof(hdr)) || hdr[0] != 'S') goto fail;
    p->w = (uint16_t)(hdr[4] | hdr[5] << 8);
    p->h = (uint16_t)(hdr[6] | hdr[7] << 8);
    p->px = plat_alloc((size_t)p->w * p->h);
    if(!p->px) goto fail;
    p->loaded = -1;
    return p;
fail:
    plat_asset_close(p->file);
    plat_free(p);
    return NULL;
}

static void pam_render(void* state, Frame* f, uint32_t t_ms) {
    Pam* p = state;
    int tick = clampi((int)(t_ms * 70 / 4000), 0, PAM_TICKS - 1);
    int frame = clampi(tick, 0, PAM_FRAMES - 1);
    if(frame != p->loaded) {
        uint32_t size = (uint32_t)p->w * p->h;
        if(plat_asset_read(p->file, 12 + frame * size, p->px, size)) p->loaded = frame;
    }
    uint8_t lut[256];
    lut_fade_white(lut, whiteness[tick] * 256 / 63);
    Image img = {.w = p->w, .h = p->h, .frames = 1, .px = p->px};
    image_draw(f, &img, 0, 0, 0, lut); /* full screen: the letterbox band */
}

static void pam_deinit(void* state) {
    Pam* p = state;
    plat_asset_close(p->file);
    plat_free(p->px);
    plat_free(p);
}

const Scene scene_pam = {
    .name = "PAM Explosion",
    .duration_ms = PAM_MS,
    .init = pam_init,
    .render = pam_render,
    .deinit = pam_deinit,
};
