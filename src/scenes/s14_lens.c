#include "scene.h"
#include "../timeline.h"
#include "../gfx/image.h"

#include <string.h>

/* A tinted glass lens drops onto the face picture and bounces around it,
 * magnifying what's underneath. The lens map (from the original's
 * per-row refraction tables) gives, for each pixel of the lens, which
 * background pixel it shows and which tint; the lens starts opaque and
 * becomes see-through. */

#define LENS_MS    (SYNC_LENS_ROTO - SYNC_LENS_BOUNCE)
#define LENS_HALFW 76 /* original lens is 152x116 */
#define LENS_HALFH 58

typedef struct {
    Image bg; /* 4 frames: plain, tint 1..3 */
    uint8_t* lmap;
    int w, h;
    uint8_t tint[3];
    const int8_t* cells; /* w*h x (dx, dy, class) */

    int32_t tick;
    int32_t x, y, xa, ya; /* lens centre, 1/64 original px */
    bool first_bounce;
} Lens;

static void lens_reset(Lens* l) {
    l->tick = -1;
    l->x = 65 * 64;
    l->y = -50 * 64;
    l->xa = l->ya = 64;
    l->first_bounce = true;
}

/* falls in, bounces off the floor (losing energy) and the side limits */
static void lens_step(Lens* l) {
    int32_t t = ++l->tick;
    l->x += l->xa;
    l->y += l->ya;
    if(l->x > 256 * 64 || l->x < 60 * 64) l->xa = -l->xa;
    if(l->y > 150 * 64 && t < 600) {
        l->y -= l->ya;
        l->ya = l->first_bounce ? -l->ya * 2 / 3 : -l->ya * 9 / 10;
        l->first_bounce = false;
    }
    l->ya += 2;
}

static void* lens_init(const Scene* self) {
    (void)self;
    Lens* l = plat_alloc(sizeof(Lens));
    if(!l) return NULL;
    size_t size = 0;
    l->lmap = plat_load_asset("lens.lmap", &size);
    if(!l->lmap || size < 9 || memcmp(l->lmap, "SRLM", 4) || !image_load(&l->bg, "lens4.srg") ||
       l->bg.frames < 4) {
        image_free(&l->bg);
        plat_free(l->lmap);
        plat_free(l);
        return NULL;
    }
    l->w = l->lmap[4];
    l->h = l->lmap[5];
    memcpy(l->tint, l->lmap + 6, 3);
    l->cells = (const int8_t*)(l->lmap + 9);
    if(size < 9 + (size_t)l->w * l->h * 3) {
        image_free(&l->bg);
        plat_free(l->lmap);
        plat_free(l);
        return NULL;
    }
    lens_reset(l);
    return l;
}

static void lens_render(void* state, Frame* f, uint32_t t_ms) {
    Lens* l = state;
    int32_t tick = (int32_t)frame70(t_ms);
    if(tick < l->tick) lens_reset(l);
    /* the drawn position is the one before this tick's move */
    int32_t cx = l->x, cy = l->y;
    while(l->tick < tick) {
        cx = l->x;
        cy = l->y;
        lens_step(l);
    }

    const Rect v = VIEW_43;
    gfx_letterbox(f, v, 0);
    image_draw(f, &l->bg, 0, v.x, v.y, NULL);

    /* see-through amount: opaque tint at first, clear after ~96 ticks */
    int clear = clampi((tick - 32) / 2 * 256 / 31, 0, 256);
    int tlx = (int)((cx / 64 - LENS_HALFW) * v.w / 320);
    int tly = (int)((cy / 64 - LENS_HALFH) * v.h / 200);
    for(int y = 0; y < l->h; y++) {
        int dy = tly + y;
        if(dy < 0 || dy >= v.h) continue;
        for(int x = 0; x < l->w; x++) {
            int dx = tlx + x;
            if(dx < 0 || dx >= v.w) continue;
            const int8_t* c = l->cells + (y * l->w + x) * 3;
            int cls = (uint8_t)c[2];
            if(cls < 1 || cls > 3) continue;
            int sx = clampi(dx + c[0], 0, v.w - 1), sy = clampi(dy + c[1], 0, v.h - 1);
            int seen = image_frame(&l->bg, cls)[sy * v.w + sx];
            int tint = l->tint[cls - 1];
            f->px[v.y + dy][v.x + dx] = (uint8_t)(tint + ((seen - tint) * clear >> 8));
        }
    }
}

static void lens_deinit(void* state) {
    Lens* l = state;
    image_free(&l->bg);
    plat_free(l->lmap);
    plat_free(l);
}

const Scene scene_lens = {
    .name = "Lens",
    .duration_ms = LENS_MS,
    .init = lens_init,
    .render = lens_render,
    .deinit = lens_deinit,
};
