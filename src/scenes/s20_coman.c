#include "scene.h"
#include "../timeline.h"

#include <string.h>

/* Voxel landscape: for every screen column a ray marches away from the
 * camera over a terrain made of two wave height maps (sampled at two
 * different positions and summed, plus a ripple with distance). Whenever
 * the terrain is higher than the ray, pixels are drawn upward and the ray
 * climbs - that's the perspective. The camera wanders as it turns; the
 * landscape rises into view at the start and sinks away at the end. */

#define SCROLL_DN_MS (SYNC_COMAN_SCROLL_DOWN - SYNC_COMAN_START)
#define COMAN_MS     (SCROLL_DN_MS + 160 * 1000 / 35) /* sinks away in 160 ticks */
#define STEPS        192
#define FAR_STEP     64 /* from here on, take double steps */
#define MAPW         128 /* height maps: 128 x 128, x halved from 256 */
/* The original's 160 columns fill our width. For its proportions (320x200
 * shown at 4:3), its 200 rows then need 96 px: the top 32, nearly all
 * empty sky, are cut off. */
#define ROWS_PX  96
#define CROP_PX  32
#define CROP_ROW (CROP_PX * 200 / ROWS_PX) /* first original row on screen */

typedef struct {
    int8_t* map[2]; /* separate blocks: one 32 KB block often isn't free */
    int8_t zwave[STEPS];
    uint8_t lum[256];

    /* camera simulation, stepped at 35 Hz */
    int32_t tick;
    int32_t rot, rot2;
    int32_t xwav, ywav;
    int32_t rise;
} Coman;

static void coman_reset(Coman* c) {
    c->tick = -1;
    c->rot = c->rot2 = 0;
    c->xwav = c->ywav = 0;
    c->rise = 120;
}

static inline int map_at(const int8_t* map, int32_t pos) {
    int32_t p = (pos >> 1) & 32767; /* x in the low byte, row above */
    return map[(p >> 8) * MAPW + ((p & 255) >> 1)];
}

/* Column direction for screen column x (-80..79) at the current turn. */
static void column_dir(const Coman* c, int x, int32_t* xa, int32_t* ya) {
    int32_t r = c->rot >> 3;
    int32_t rs = fx_sin(r) * 256 / FX_ONE14, rc = fx_cos(r) * 256 / FX_ONE14;
    int32_t rs2 = fx_sin(r + 177) * 256 / FX_ONE14, rc2 = fx_cos(r + 177) * 256 / FX_ONE14;
    const int y = 160;
    *xa = (x * rc + y * rs) / 256;
    *ya = (y * rc2 - x * rs2) / 256;
}

static void coman_step(Coman* c, uint32_t t_ms) {
    c->tick++;
    /* camera turns at half the frame rate, as in the original */
    if(!(c->tick & 1)) {
        c->rot2 += 4;
        c->rot += fx_sin(c->rot2) * 256 / FX_ONE14 / 15;
    }
    int32_t xa, ya;
    column_dir(c, 0, &xa, &ya);
    c->xwav += xa * 2;
    c->ywav += ya * 2;
    if(c->tick < 400) {
        if(c->rise > 0) c->rise--;
    } else if(t_ms >= SCROLL_DN_MS && c->rise < 160) {
        c->rise++;
    }
}

static void* coman_init(const Scene* self) {
    (void)self;
    Coman* c = plat_alloc(sizeof(Coman));
    if(!c) return NULL;
    PlatFile* file = plat_asset_open("coman.bin");
    bool ok = file != NULL;
    for(int m = 0; m < 2 && ok; m++) {
        c->map[m] = plat_alloc(MAPW * MAPW);
        ok = c->map[m] && plat_asset_read(file, (uint32_t)m * MAPW * MAPW, c->map[m], MAPW * MAPW);
    }
    plat_asset_close(file);
    if(!ok) {
        plat_free(c->map[0]);
        plat_free(c->map[1]);
        plat_free(c);
        return NULL;
    }
    for(int i = 0; i < STEPS; i++)
        c->zwave[i] = (int8_t)(16 * fx_sin(i * 3 * FX_ANGLE_STEPS / STEPS) / FX_ONE14);

    /* palette: blue-green water/land ramp by height and distance, then
     * a dark red band at the top indices */
    uint8_t pal[256][3];
    memset(pal, 0, sizeof(pal));
    for(int a = 0; a < 256; a++) {
        int uc = 223 - a * 22 / 26;
        if(uc < 0) continue;
        int g = (230 - a) / 4 + fx_sin(a * 4) * 256 / FX_ONE14 / 32;
        int b = (255 - a) / 3;
        int r = a - 220 < 0 ? 220 - a : a - 220;
        pal[uc][1] = (uint8_t)clampi(g, 0, 63);
        pal[uc][2] = (uint8_t)clampi(b, 0, 63);
        pal[uc][0] = (uint8_t)((40 - (r > 40 ? 40 : r)) / 3);
    }
    for(int i = 0; i < 240; i++)
        for(int k = 0; k < 3; k++)
            pal[i][k] = (uint8_t)clampi(pal[i][k] * 9 / 6, 0, 63);
    for(int a = 0; a < 24; a++) {
        pal[255 - a][0] = (uint8_t)((a - 4 < 0 ? 0 : a - 4) / 2);
        pal[255 - a][1] = pal[255 - a][2] = 0;
    }
    pal[0][0] = pal[0][1] = pal[0][2] = 0;
    for(int i = 0; i < 256; i++)
        c->lum[i] = vga_lum(pal[i][0], pal[i][1], pal[i][2]);

    coman_reset(c);
    return c;
}

__attribute__((optimize("O2"))) static void coman_render(void* state, Frame* f, uint32_t t_ms) {
    Coman* c = state;
    int32_t tick = (int32_t)(t_ms * 35 / 1000);
    if(tick < c->tick) coman_reset(c);
    while(c->tick < tick)
        coman_step(c, t_ms);

    gfx_clear(f, 0);
    const int8_t* m1 = c->map[0];
    const int8_t* m2 = c->map[1];
    /* bottom of the landscape, in original rows; it rises from below */
    const int bottom = c->rise + 22 + 199;

    /* like the original (160 columns on a 320-wide screen), each ray
     * draws a column two pixels wide */
    for(int fx = 0; fx < FB_W; fx += 2) {
        int32_t xa, ya;
        column_dir(c, (fx - FB_W / 2) * 160 / FB_W, &xa, &ya);
        xa &= ~1;
        ya &= ~1;
        int32_t xw = c->xwav & ~1, yw = c->ywav & ~1;
        /* ray height and its slope in 16.16, as the original's registers */
        int32_t ray = 0, dir = -(200 - 70) * 2560;
        int row = bottom; /* next original row to fill, going up */
        const int top = c->rise + 22 > CROP_ROW ? c->rise + 22 : CROP_ROW;

        for(int j = 0; j < STEPS && row > top; j++) {
            int far = j >= FAR_STEP;
            if(j == FAR_STEP) {
                xa *= 2;
                ya *= 2;
            }
            xw += xa;
            yw += ya;
            int32_t h = (int32_t)(map_at(m1, xw) + map_at(m2, yw) + c->zwave[j] - 240) * 65536;
            if(ray < h) {
                /* the terrain pokes above the ray: draw the rows it covers
                 * (each drawn row raises the ray by l) in one span */
                uint8_t v = c->lum[(((h >> 16) + 140 - j / 8) & 255) >> 1];
                int32_t l = j ? j * 2560 : 1024;
                int n = (int)((h - ray + l - 1) / l);
                if(n > row - top) n = row - top;
                int fy0 = clampi((row - n + 1) * ROWS_PX / 200 - CROP_PX, 0, FB_H);
                int fy1 = clampi(row * ROWS_PX / 200 - CROP_PX, -1, FB_H - 1);
                for(int fy = fy0; fy <= fy1; fy++)
                    f->px[fy][fx] = f->px[fy][fx + 1] = v;
                ray += n * l;
                dir += n * 2560;
                row -= n;
            }
            ray += far ? 2 * dir : dir;
            if(far) j++;
        }
    }
}

static void coman_deinit(void* state) {
    Coman* c = state;
    plat_free(c->map[0]);
    plat_free(c->map[1]);
    plat_free(c);
}

const Scene scene_coman = {
    .name = "Coman Voxel",
    .duration_ms = COMAN_MS,
    .init = coman_init,
    .render = coman_render,
    .deinit = coman_deinit,
};
