#include "scene.h"
#include "glenz_common.h"
#include "../gfx/vec3.h"

#include <string.h>

/* Glenz vectors: a translucent blue/white tetrakis hexahedron drops onto
 * the checkerboard, bounces and wobbles like jelly, then a second red one
 * grows inside/around it and they orbit until both shrink away.
 *
 * Translucency works like the original: polygons are OR-ed into an index
 * buffer (face * 8 + the 3 bits of whatever is behind) and a palette that
 * is rebuilt as faces are lit mixes each face colour with a quarter of the
 * colour behind it. The index buffer is then mapped through the palette. */

#define GLENZ_TICKS 2164
#define MAX_VERTS   16
#define MAX_FACES   32

typedef struct {
    uint8_t code, n, idx[4];
} Face;

typedef struct {
    int nverts, nfaces;
    Vec3 v[MAX_VERTS];
    Face face[MAX_FACES];
} Poly3D;

/* What moves smoothly between ticks: drawn interpolated (we draw at ~46
 * fps, the motion runs at 70 ticks per second). */
typedef struct {
    float ypos, s1x, s1y, s1z, s2;
    Vec3 t1, t2;
} Motion;

typedef struct {
    Checker checker;
    Poly3D obj[2];
    int32_t board_pos; /* where the transition left the board */
    uint8_t idx[FB_H][FB_W];

    /* simulation */
    int32_t tick;
    float ypos, yposa, jello, jelloa;
    float jello_y, jello_ya; /* damped copy of the wobble, for the height */
    int boingm, boingd;
    float s1x, s1y, s1z, s2;
    Vec3 t1, t2;
    float hover_v; /* velocity while settling to the hover height */
    bool hovering;
    Motion prev; /* motion values one tick earlier */
    int lightshift;
    bool board_cleared;
    uint8_t backpal[8][3];
    uint8_t pal[256][3]; /* live palette, 6-bit */
} Glenz;

static bool load_objects(Glenz* g) {
    size_t size = 0;
    uint8_t* d = plat_load_asset("glenz.bin", &size);
    if(!d) return false;
    size_t i = 0;
    bool ok = true;
    for(int o = 0; o < 2 && ok; o++) {
        Poly3D* p = &g->obj[o];
        p->nverts = d[i++];
        ok = p->nverts <= MAX_VERTS && i + p->nverts * 6 < size;
        for(int k = 0; ok && k < p->nverts; k++, i += 6) {
            p->v[k].x = (int16_t)(d[i] | d[i + 1] << 8);
            p->v[k].y = (int16_t)(d[i + 2] | d[i + 3] << 8);
            p->v[k].z = (int16_t)(d[i + 4] | d[i + 5] << 8);
        }
        p->nfaces = ok ? d[i++] : 0;
        ok = ok && p->nfaces <= MAX_FACES;
        for(int k = 0; ok && k < p->nfaces; k++) {
            Face* fc = &p->face[k];
            fc->code = d[i++];
            fc->n = d[i++];
            ok = fc->n >= 3 && fc->n <= 4 && i + fc->n <= size;
            for(int j = 0; ok && j < fc->n; j++)
                fc->idx[j] = d[i++];
        }
    }
    plat_free(d);
    return ok;
}

static void glenz_reset(Glenz* g) {
    g->tick = -1;
    g->ypos = -9000;
    g->yposa = 0;
    g->jello = g->jelloa = 0;
    g->jello_y = g->jello_ya = 0;
    g->boingm = 6;
    g->boingd = 7;
    g->s1x = g->s1y = g->s1z = 120;
    g->s2 = 0;
    g->t1 = g->t2 = (Vec3){0, 0, 0};
    g->hover_v = 0;
    g->hovering = false;
    g->lightshift = 9;
    g->board_cleared = false;
    memset(g->pal, 0, sizeof(g->pal));
    for(int i = 0; i < 16; i++)
        memcpy(g->pal[i], g->checker.pal + i * 3, 3);
    for(int i = 0; i < 8; i++)
        memcpy(g->backpal[i], g->checker.pal + i * 3, 3);
}

static float s255(int32_t a) {
    return fx_sin(a) * 255.0f / FX_ONE14;
}

static Motion motion_now(const Glenz* g) {
    return (Motion){g->ypos, g->s1x, g->s1y, g->s1z, g->s2, g->t1, g->t2};
}

static void glenz_step(Glenz* g) {
    g->prev = motion_now(g);
    int32_t f = ++g->tick;

    if(f < 800) { /* drop, bounce with jelly squash, then rise to hover */
        if(f < 710) {
            g->yposa += 31;
            g->ypos += g->yposa / 40;
            if(g->ypos > -300) {
                g->ypos -= g->yposa / 40;
                g->yposa = -g->yposa * g->boingm / g->boingd;
                g->boingm += 2;
                g->boingd++;
            }
            if(g->ypos > -900 && g->yposa > 0) {
                g->jello = g->jello_y = (g->ypos + 900) * 5 / 3;
                g->jelloa = g->jello_ya = 0;
            }
        } else {
            /* Settle at the hover height. The original jumps from the
             * bounce straight to a constant 16/tick slide, reversing an
             * upward 75/tick in one tick; a damped spring that starts from
             * the real velocity eases into place instead. */
            if(!g->hovering) {
                g->hovering = true;
                g->hover_v = g->yposa / 40;
            }
            g->hover_v = g->hover_v * 0.88f + (-2800 - g->ypos) * 0.004f;
            g->ypos += g->hover_v;
        }
        g->s1x = 120 + g->jello / 30;
        g->s1y = 120 + g->jello_y / 30;
        g->s1z = 120 - g->jello / 30;
        /* The jelly wobble is a spring, damped (as in the original) only
         * when it passes through rest, so it keeps wobbling after a landing.
         * Width and depth use it as is. The height uses a copy damped every
         * tick: with the full wobble the height swells again while the
         * object rises, and its top leaps upward. Both start from the same
         * squash on landing. */
        float a = g->jello;
        g->jello += g->jelloa;
        if((a < 0 && g->jello > 0) || (a > 0 && g->jello < 0)) g->jelloa = g->jelloa * 5 / 6;
        g->jelloa -= g->jello / 20;
        g->jello_y += g->jello_ya;
        g->jello_ya *= 0.8f;
        g->jello_ya -= g->jello_y / 20;
    }
    if(f > 900) { /* small orbit once the second one is here */
        int32_t a = f - 900, b = a > 50 ? 50 : a;
        g->t1.x = s255(a * 3) * b / 10;
        g->t1.y = s255(a * 5) * b / 10;
        g->t1.z = (s255(a * 4) / 2 + 128) * b / 16;
    }
    if(f > 1800) { /* leave through the top, then shrink */
        int32_t b = 1800 - f < -99 ? -99 : 1800 - f;
        g->t1.y -= b * b / 2.0f;
        if(f > 2009) {
            if(g->s1x > 0) g->s1x -= 1;
            if(g->s1y > 0) g->s1y -= 1;
            if(g->s1z > 0) g->s1z -= 1;
        }
    }

    if(f > 1800) {
        int32_t a = f - 1800 + 64;
        g->t2 = (Vec3){-s255(a * 6) * a / 40, -s255(a * 7) * a / 40, (s255(a * 8) + 128) * a / 40};
    } else if(f > 900) {
        int32_t a = f - 900;
        g->t2 = (Vec3){-s255(a * 6), -s255(a * 7), s255(a * 8) + 128};
    }
    if(f > 800 && f <= 890) g->s2 += 2;
    if(f > 2009 && g->s2 > 0) g->s2 -= 1;
    if(g->s2 > g->s1x) g->lightshift = 10;

    if(f > 700) { /* board fades out; palette switches to the red mix */
        if(f < 765) {
            int b = 764 - f;
            for(int i = 0; i < 8; i++)
                for(int c = 0; c < 3; c++)
                    g->pal[i][c] = (uint8_t)(g->backpal[i][c] * b / 64);
        } else if(f == 765) {
            g->board_cleared = true;
        } else if(f == 790) {
            for(int i = 0; i < 8; i++) {
                int r = (i & 1 ? 10 : 0) + (i & 2 ? 30 : 0) + (i & 4 ? 20 : 0);
                g->backpal[i][0] = g->pal[i][0] = (uint8_t)(r > 63 ? 63 : r);
                g->backpal[i][1] = g->backpal[i][2] = g->pal[i][1] = g->pal[i][2] = 0;
            }
        } else if(f > 2069) {
            int b = 2133 - f < 0 ? 0 : 2133 - f;
            for(int i = 0; i < 8; i++)
                for(int c = 0; c < 3; c++)
                    g->pal[i][c] = (uint8_t)(g->backpal[i][c] * b / 64);
        }
    }
}

static void* glenz_init(const Scene* self) {
    (void)self;
    Glenz* g = plat_alloc(sizeof(Glenz));
    if(!g) return NULL;
    if(!checker_load(&g->checker) || !load_objects(g)) {
        checker_free(&g->checker);
        plat_free(g);
        return NULL;
    }
    int32_t vel = 0;
    g->board_pos = 0;
    while(checker_bounce_step(&g->board_pos, &vel)) {
    }
    glenz_reset(g);
    return g;
}

/* OR a polygon into the index buffer. */
static void fill_or(Glenz* g, const Vec2* pts, int n, uint8_t v) {
    gfx_fill_convex((Frame*)g->idx, pts, n, v, FillOr);
}

static void draw_object(Glenz* g, int which, const float m[9], float scale, Vec3 t, bool squash) {
    const Poly3D* p = &g->obj[which];
    Vec2 orig[MAX_VERTS], scr[MAX_VERTS];
    for(int i = 0; i < p->nverts; i++) {
        Vec3 r = mat_apply(m, p->v[i]);
        float sx = which == 0 ? g->s1x : scale, sy = which == 0 ? g->s1y : scale;
        float sz = which == 0 ? g->s1z : scale;
        r = (Vec3){r.x * sx * 64 / 32768 + t.x, r.y * sy * 64 / 32768 + t.y, r.z * sz * 64 / 32768 + t.z};
        if(squash && r.y > 1500) r.y = 1500; /* flattened against the floor */
        orig[i] = (Vec2){r.x * 256 / r.z + 160, r.y * 213 / r.z + 130};
        scr[i] = vga43(orig[i].x, orig[i].y);
    }

    for(int fi = 0; fi < p->nfaces; fi++) {
        const Face* fc = &p->face[fi];
        const Vec2 *a = &orig[fc->idx[0]], *b = &orig[fc->idx[1]], *c = &orig[fc->idx[2]];
        /* facing and lighting from the projected winding, in original px */
        float normal = (float)((int)a->x - (int)b->x) * ((int)a->y - (int)c->y) -
                       (float)((int)a->y - (int)b->y) * ((int)a->x - (int)c->x);
        uint8_t colour;
        if(which == 0) {
            int face = fc->code >> 1; /* 1..24, odd = blue, even = white */
            if(normal < 0) {
                if(!(face & 1)) continue; /* white back faces aren't drawn */
                colour = 4;
            } else {
                /* original divides by 128 (170 later); brighter here so faces read on the LCD */
                int s = (int)(g->lightshift == 9 ? normal / 48 : normal / 64);
                s = clampi(s, 0, 63);
                int r = s, gg = s, bb = s;
                if(face & 1) {
                    r = 7;
                    gg = s / 2;
                }
                colour = (uint8_t)(face * 8);
                for(int i = 0; i < 8; i++) {
                    uint8_t* e = g->pal[colour + i];
                    e[0] = (uint8_t)clampi(r + g->backpal[i][0] / 4, 0, 63);
                    e[1] = (uint8_t)clampi(gg + g->backpal[i][1] / 4, 0, 63);
                    e[2] = (uint8_t)clampi(bb + g->backpal[i][2] / 4, 0, 63);
                }
            }
        } else {
            colour = normal >= 0 ? (fc->code >> 1) & 1 : fc->code;
            if(!colour) continue;
        }
        Vec2 pts[4];
        for(int j = 0; j < fc->n; j++)
            pts[j] = scr[fc->idx[j]];
        fill_or(g, pts, fc->n, colour);
    }
}

static float lerpf(float a, float b, float t) {
    return a + (b - a) * t;
}

static Vec3 lerp3(Vec3 a, Vec3 b, float t) {
    return (Vec3){lerpf(a.x, b.x, t), lerpf(a.y, b.y, t), lerpf(a.z, b.z, t)};
}

static void glenz_render(void* state, Frame* f, uint32_t t_ms) {
    Glenz* g = state;
    /* run the simulation one tick ahead and draw between the two ticks */
    float tf = t_ms * 70 / 1000.0f;
    if(tf > GLENZ_TICKS - 1) tf = GLENZ_TICKS - 1;
    int32_t tick = (int32_t)tf;
    int32_t ahead = tick + 1 < GLENZ_TICKS ? tick + 1 : tick;
    if(ahead < g->tick) glenz_reset(g);
    while(g->tick < ahead)
        glenz_step(g);
    float frac = ahead > tick ? tf - tick : 1.0f;
    Motion cur = motion_now(g), p = g->prev;
    g->ypos = lerpf(p.ypos, cur.ypos, frac);
    g->s1x = lerpf(p.s1x, cur.s1x, frac);
    g->s1y = lerpf(p.s1y, cur.s1y, frac);
    g->s1z = lerpf(p.s1z, cur.s1z, frac);
    g->s2 = lerpf(p.s2, cur.s2, frac);
    g->t1 = lerp3(p.t1, cur.t1, frac);
    g->t2 = lerp3(p.t2, cur.t2, frac);

    /* background: the settled board until it's cleared, in the 4:3
     * window (like the title picture and the transition before it) */
    const Rect v = VIEW_43;
    memset(g->idx, 0, sizeof(g->idx));
    if(!g->board_cleared) {
        const Image* b = &g->checker.board;
        for(int y = 0; y < v.h; y++) {
            int row = checker_row(g->board_pos, (int)(y / VGA43_SY), false);
            if(row < 0) continue;
            const uint8_t* src = image_frame(b, 0) + row * b->w;
            for(int x = 0; x < v.w; x++)
                g->idx[v.y + y][v.x + x] = src[x * b->w / v.w];
        }
    }

    int32_t rx = (int32_t)(32 * tf) % 10800, ry = (int32_t)(7 * tf) % 10800;
    float m[9];
    if(g->s1x > 4) {
        mat_rot_yxz(m, rx, ry, 0);
        Vec3 t = {g->t1.x, g->ypos + 1500 + g->t1.y, 7500 + g->t1.z};
        draw_object(g, 0, m, 0, t, tick < 800);
    }
    if(tick > 800 && g->s2 > 4) {
        mat_rot_yxz(m, 3600 - rx / 3, 3600 - ry / 3, 3600);
        Vec3 t = {g->t2.x, g->ypos + 1500 + g->t2.y, 7500 + g->t2.z};
        draw_object(g, 1, m, g->s2, t, false);
    }

    uint8_t lut[256];
    for(int i = 0; i < 256; i++)
        lut[i] = vga_lum(g->pal[i][0], g->pal[i][1], g->pal[i][2]);
    for(int y = 0; y < FB_H; y++)
        for(int x = 0; x < FB_W; x++)
            f->px[y][x] = lut[g->idx[y][x]];
    gfx_letterbox(f, v, 0); /* nothing exists outside the 4:3 original */

    /* back to the simulation's own values for the next step */
    g->ypos = cur.ypos;
    g->s1x = cur.s1x;
    g->s1y = cur.s1y;
    g->s1z = cur.s1z;
    g->s2 = cur.s2;
    g->t1 = cur.t1;
    g->t2 = cur.t2;
}

static void glenz_deinit(void* state) {
    Glenz* g = state;
    checker_free(&g->checker);
    plat_free(g);
}

const Scene scene_glenz = {
    .name = "Glenz 3D",
    .duration_ms = GLENZ_TICKS * 1000 / 70,
    .init = glenz_init,
    .render = glenz_render,
    .deinit = glenz_deinit,
};
