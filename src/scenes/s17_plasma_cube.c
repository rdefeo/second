#include "scene.h"
#include "../timeline.h"
#include "../gfx/vec3.h"

#include <string.h>

/* Plasma cube: a cube flies and tumbles along a spline track. Each visible
 * face carries an animated, wobbling sine-plasma texture in one of three
 * colour schemes, lit by a light that also moves along the track. */

#define CUBE_MS    (SYNC_PLZ_CUBE_END - SYNC_PLZ_CUBE_START)
#define CUBE_SIZE  125
#define SINI_LEN   512

typedef struct {
    int16_t coef[1024];
    int nkeys;
    int16_t (*key)[8];
    uint8_t* blob;
    int8_t sini[SINI_LEN]; /* sin(4*pi*a/1024) * 127 */
    uint8_t shade[3][64]; /* palette brightness per face scheme, unlit */
} Cube;

/* Corners and faces; each face uses texture corners (64,4)..(190,60). */
static const int8_t corner[8][3] = {
    {1, 1, 1}, {1, -1, 1}, {-1, -1, 1}, {-1, 1, 1},
    {1, 1, -1}, {1, -1, -1}, {-1, -1, -1}, {-1, 1, -1}};
static const uint8_t face_idx[6][4] = {
    {1, 2, 3, 0}, {7, 6, 5, 4}, {0, 4, 5, 1}, {1, 5, 6, 2}, {2, 6, 7, 3}, {3, 7, 4, 0}};
static const uint8_t face_scheme[6] = {0, 0, 1, 2, 1, 2};
static const int16_t tex_uv[4][2] = {{64, 4}, {190, 4}, {190, 60}, {64, 60}};

static void* cube_init(const Scene* self) {
    (void)self;
    Cube* c = plat_alloc(sizeof(Cube));
    if(!c) return NULL;
    size_t size = 0;
    c->blob = plat_load_asset("plzcube.bin", &size);
    if(!c->blob || size < 2 + 2048 + 2) goto fail;
    if((c->blob[0] | c->blob[1] << 8) != 1024) goto fail;
    memcpy(c->coef, c->blob + 2, sizeof(c->coef));
    c->nkeys = c->blob[2050] | c->blob[2051] << 8;
    if(size < 2052 + (size_t)c->nkeys * 16) goto fail;
    c->key = (int16_t(*)[8])(c->blob + 2052);

    for(int a = 0; a < SINI_LEN; a++)
        c->sini[a] = (int8_t)(fx_sin(a * 2) * 127 / FX_ONE14);
    /* three schemes, each a dark->bright ramp then a second hue ramp */
    for(int a = 0; a < 32; a++) {
        c->shade[0][a] = vga_lum(0, 0, a * 2); /* blue */
        c->shade[0][a + 32] = vga_lum(a * 2, a * 2, 63); /* -> white */
        c->shade[1][a] = vga_lum(a * 2, 0, 0); /* red */
        c->shade[1][a + 32] = vga_lum(63, a * 2, 0); /* -> yellow */
        c->shade[2][a] = vga_lum(a, 0, a * 2 / 3); /* purple */
        c->shade[2][a + 32] = vga_lum(31 - a, a * 2, 21); /* -> green */
    }
    return c;
fail:
    plat_free(c->blob);
    plat_free(c);
    return NULL;
}

/* Track value at 8.8 position `pos`, channel ch. */
static int32_t spline(const Cube* c, int32_t pos, int ch) {
    int i = pos >> 8, f = pos & 255;
    if(i + 3 >= c->nkeys) i = c->nkeys - 4;
    int32_t s = c->key[i + 3][ch] * c->coef[f] + c->key[i + 2][ch] * c->coef[f + 256] +
                c->key[i + 1][ch] * c->coef[f + 512] + c->key[i][ch] * c->coef[f + 768];
    return (s * 2) >> 16;
}

typedef struct {
    float x, y, u, v;
} TexVert;

/* Affine texture-mapped convex polygon; texel = plasma(u, v, wobble). */
static void tex_quad(const Cube* c, Frame* f, const TexVert* p, int n, const uint8_t* lut, int wobble) {
    float ymin = p[0].y, ymax = p[0].y;
    for(int i = 1; i < n; i++) {
        if(p[i].y < ymin) ymin = p[i].y;
        if(p[i].y > ymax) ymax = p[i].y;
    }
    int y0 = clampi((int)(ymin + 0.5f), 0, FB_H), y1 = clampi((int)(ymax + 0.5f), 0, FB_H);
    for(int y = y0; y < y1; y++) {
        float yc = y + 0.5f;
        float xl = 1e9f, xr = -1e9f, ul = 0, vl = 0, ur = 0, vr = 0;
        for(int i = 0; i < n; i++) {
            const TexVert *a = &p[i], *b = &p[(i + 1) % n];
            if((a->y <= yc && b->y > yc) || (b->y <= yc && a->y > yc)) {
                float t = (yc - a->y) / (b->y - a->y);
                float x = a->x + t * (b->x - a->x);
                float u = a->u + t * (b->u - a->u), v = a->v + t * (b->v - a->v);
                if(x < xl) xl = x, ul = u, vl = v;
                if(x > xr) xr = x, ur = u, vr = v;
            }
        }
        if(xr <= xl) continue;
        int x0 = clampi((int)(xl + 0.5f), 0, FB_W), x1 = clampi((int)(xr + 0.5f), 0, FB_W);
        float du = (ur - ul) / (xr - xl), dv = (vr - vl) / (xr - xl);
        float u = ul + (x0 + 0.5f - xl) * du, v = vl + (x0 + 0.5f - xl) * dv;
        for(int x = x0; x < x1; x++, u += du, v += dv) {
            int iv = (int)v & 63;
            /* rows wobble sideways; the wobble scrolls with time */
            int iu = ((int)u + c->sini[((wobble + iv) * 8) & (SINI_LEN - 1)] / 3) & 255;
            int t = c->sini[(iv * 4 + c->sini[(iu * 2) & (SINI_LEN - 1)]) & (SINI_LEN - 1)] / 4 + 32;
            f->px[y][x] = lut[t & 63];
        }
    }
}

static void cube_render(void* state, Frame* f, uint32_t t_ms) {
    Cube* c = state;
    gfx_clear(f, 0);
    int32_t tick = (int32_t)frame70(t_ms);
    int32_t pos = 4 * 256 + tick * 4;

    float tx = spline(c, pos, 0), ty = spline(c, pos, 1), dist = spline(c, pos, 2);
    int32_t kx = spline(c, pos, 3) & 1023, ky = spline(c, pos, 4) & 1023, kz = spline(c, pos, 5) & 1023;
    int32_t lkx = spline(c, pos, 6) & 1023, lky = spline(c, pos, 7) & 1023;
    float lx = fx_sin(lkx) / (float)FX_ONE14 * fx_sin(lky) / FX_ONE14 * 127;
    float ly = fx_cos(lkx) / (float)FX_ONE14 * 127;
    float lz = fx_sin(lkx) / (float)FX_ONE14 * fx_cos(lky) / FX_ONE14 * 127;

    float sx = fx_sin(kx) / (float)FX_ONE14, cx = fx_cos(kx) / (float)FX_ONE14;
    float sy = fx_sin(ky) / (float)FX_ONE14, cy = fx_cos(ky) / (float)FX_ONE14;
    float sz = fx_sin(kz) / (float)FX_ONE14, cz = fx_cos(kz) / (float)FX_ONE14;
    const float m[9] = {
        cy * cz, cy * sz, -sy,
        sx * sy * cz - cx * sz, sx * sy * sz + cx * cz, cy * sx,
        cx * sy * cz + sx * sz, cx * sy * sz - sx * cz, cy * cx};

    Vec3 r[8];
    TexVert s[8];
    for(int i = 0; i < 8; i++) {
        Vec3 v = {corner[i][0] * CUBE_SIZE, corner[i][1] * CUBE_SIZE, corner[i][2] * CUBE_SIZE};
        r[i] = mat_apply(m, v);
        r[i].x += tx;
        r[i].y += ty;
        r[i].z += dist;
        /* original 320x134 screen; scaled to keep the cube square */
        float ox = r[i].x * 245 / r[i].z + 160, oy = r[i].y * 137 / r[i].z + 66;
        s[i].x = ox * 0.4f;
        s[i].y = (oy - 66) * (245.0f * 0.4f / 137) + FB_H / 2;
    }

    for(int fi = 0; fi < 6; fi++) {
        const uint8_t* q = face_idx[fi];
        Vec3 a = {r[q[1]].x - r[q[0]].x, r[q[1]].y - r[q[0]].y, r[q[1]].z - r[q[0]].z};
        Vec3 b = {r[q[2]].x - r[q[0]].x, r[q[2]].y - r[q[0]].y, r[q[2]].z - r[q[0]].z};
        float nx = a.y * b.z - a.z * b.y, ny = a.z * b.x - a.x * b.z, nz = a.x * b.y - a.y * b.x;
        /* Facing away when the (inward) normal points back at the eye. With
         * the cube this close to the camera, testing nz alone (orthographic)
         * lets hidden side faces draw over the front ones. */
        if(nx * r[q[0]].x + ny * r[q[0]].y + nz * r[q[0]].z < 0) continue;
        int light = clampi((int)((lx * nx + ly * ny + lz * nz) / 250000) + 32, 0, 64);
        uint8_t lut[64];
        for(int i = 0; i < 64; i++)
            lut[i] = (uint8_t)(c->shade[face_scheme[fi]][i] * light >> 6);
        TexVert p[4];
        for(int j = 0; j < 4; j++) {
            p[j] = s[q[j]];
            p[j].u = tex_uv[j][0];
            p[j].v = tex_uv[j][1];
        }
        tex_quad(c, f, p, 4, lut, tick & 63);
    }
}

static void cube_deinit(void* state) {
    Cube* c = state;
    plat_free(c->blob);
    plat_free(c);
}

const Scene scene_plasma_cube = {
    .name = "Plasma Cube",
    .duration_ms = CUBE_MS,
    .init = cube_init,
    .render = cube_render,
    .deinit = cube_deinit,
};
