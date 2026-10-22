#include "gfx.h"

#include <string.h>

uint32_t isqrt32(uint32_t v) {
    uint32_t res = 0;
    uint32_t bit = 1UL << 30;
    while(bit > v)
        bit >>= 2;
    while(bit) {
        if(v >= res + bit) {
            v -= res + bit;
            res = (res >> 1) + bit;
        } else {
            res >>= 1;
        }
        bit >>= 2;
    }
    return res;
}

void gfx_clear(Frame* f, uint8_t v) {
    memset(f->px, v, sizeof(f->px));
}

void gfx_fill_rect(Frame* f, int x, int y, int w, int h, uint8_t v) {
    int x0 = clampi(x, 0, FB_W), x1 = clampi(x + w, 0, FB_W);
    int y0 = clampi(y, 0, FB_H), y1 = clampi(y + h, 0, FB_H);
    if(x1 <= x0) return;
    for(int yy = y0; yy < y1; yy++)
        memset(&f->px[yy][x0], v, x1 - x0);
}

void gfx_letterbox(Frame* f, Rect view, uint8_t v) {
    gfx_fill_rect(f, 0, 0, FB_W, view.y, v);
    gfx_fill_rect(f, 0, view.y + view.h, FB_W, FB_H - view.y - view.h, v);
    gfx_fill_rect(f, 0, view.y, view.x, view.h, v);
    gfx_fill_rect(f, view.x + view.w, view.y, FB_W - view.x - view.w, view.h, v);
}

void gfx_hline(Frame* f, int x0, int x1, int y, uint8_t v) {
    if((unsigned)y >= FB_H) return;
    if(x0 > x1) {
        int t = x0;
        x0 = x1;
        x1 = t;
    }
    x0 = clampi(x0, 0, FB_W - 1);
    x1 = clampi(x1, 0, FB_W - 1);
    memset(&f->px[y][x0], v, x1 - x0 + 1);
}

void gfx_line(Frame* f, int x0, int y0, int x1, int y1, uint8_t v) {
    int dx = x1 > x0 ? x1 - x0 : x0 - x1;
    int dy = y1 > y0 ? y0 - y1 : y1 - y0;
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for(;;) {
        gfx_pset(f, x0, y0, v);
        if(x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if(e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if(e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

void gfx_disc(Frame* f, int cx, int cy, int r, uint8_t v) {
    for(int dy = -r; dy <= r; dy++) {
        int dx = (int)isqrt32((uint32_t)(r * r - dy * dy));
        gfx_hline(f, cx - dx, cx + dx, cy + dy, v);
    }
}

#if defined(__ARM_FEATURE_SIMD32)
#include <arm_acle.h>
#endif

/* Spans work on whole 32-bit words where they can: 4 pixels at a time. */
__attribute__((optimize("O2"))) static void
    fill_span(uint8_t* row, int x0, int x1, uint8_t v, FillMode mode) {
    if(mode == FillSet) {
        memset(row + x0, v, x1 - x0);
        return;
    }
    uint32_t v4 = v * 0x01010101u;
    int x = x0;
    for(; x < x1 && (x & 3); x++) /* up to the first aligned word */
        row[x] = mode == FillOr ? row[x] | v : mode == FillXor ? row[x] ^ v :
                                  (row[x] + v > 255 ? 255 : row[x] + v);
    uint32_t* w = (uint32_t*)(row + x);
    int words = (x1 - x) >> 2;
    switch(mode) {
    case FillOr:
        for(int i = 0; i < words; i++)
            w[i] |= v4;
        break;
    case FillXor:
        for(int i = 0; i < words; i++)
            w[i] ^= v4;
        break;
    default: /* FillAdd: saturating, 4 bytes at once on the Cortex-M4 */
#if defined(__ARM_FEATURE_SIMD32)
        for(int i = 0; i < words; i++)
            w[i] = __uqadd8(w[i], v4);
#else
        for(int i = 0; i < words * 4; i++)
            row[x + i] = row[x + i] + v > 255 ? 255 : row[x + i] + v;
#endif
        break;
    }
    for(x += words * 4; x < x1; x++) /* the tail */
        row[x] = mode == FillOr ? row[x] | v : mode == FillXor ? row[x] ^ v :
                                  (row[x] + v > 255 ? 255 : row[x] + v);
}

/* ceil() for the screen range, without libm (not exported to FAPs). */
static inline int iceil(float v) {
    if(v < -4096.0f) v = -4096.0f;
    if(v > 4096.0f) v = 4096.0f;
    int i = (int)v;
    return v > (float)i ? i + 1 : i;
}

/* Walk each edge once, stepping its x down the rows it crosses (rows whose
 * centre y + 0.5 lies in [top, bottom)), keeping per row the leftmost and
 * rightmost crossing. Optional per-vertex values are carried along. */
static int scan_edges(
    const Vec2* pts,
    const float* val,
    int n,
    float* xl,
    float* xr,
    float* vl,
    float* vr,
    int* y_lo) {
    float ymin = pts[0].y, ymax = pts[0].y;
    for(int i = 1; i < n; i++) {
        if(pts[i].y < ymin) ymin = pts[i].y;
        if(pts[i].y > ymax) ymax = pts[i].y;
    }
    int y0 = clampi(iceil(ymin - 0.5f), 0, FB_H);
    int y1 = clampi(iceil(ymax - 0.5f), 0, FB_H);
    for(int y = y0; y < y1; y++) {
        xl[y] = 1e9f;
        xr[y] = -1e9f;
    }
    for(int i = 0; i < n; i++) {
        int j = (i + 1) % n;
        const Vec2 *a = &pts[i], *b = &pts[j];
        if(a->y == b->y) continue;
        int top = a->y < b->y ? i : j, bot = top == i ? j : i;
        const Vec2 *t = &pts[top], *u = &pts[bot];
        int ys = clampi(iceil(t->y - 0.5f), y0, y1);
        int ye = clampi(iceil(u->y - 0.5f), y0, y1);
        if(ys >= ye) continue;
        float inv = 1.0f / (u->y - t->y);
        float dx = (u->x - t->x) * inv;
        float k = (ys + 0.5f - t->y) * inv; /* fraction along the edge */
        float x = t->x + (u->x - t->x) * k;
        float dv = 0, v = 0;
        if(val) {
            dv = (val[bot] - val[top]) * inv;
            v = val[top] + (val[bot] - val[top]) * k;
        }
        for(int y = ys; y < ye; y++, x += dx, v += dv) {
            if(x < xl[y]) {
                xl[y] = x;
                if(val) vl[y] = v;
            }
            if(x > xr[y]) {
                xr[y] = x;
                if(val) vr[y] = v;
            }
        }
    }
    *y_lo = y0;
    return y1;
}

__attribute__((optimize("O2"))) void gfx_fill_convex(Frame* f, const Vec2* pts, int n, uint8_t v, FillMode mode) {
    if(n < 3) return;
    float xl[FB_H], xr[FB_H];
    int y0, y1 = scan_edges(pts, NULL, n, xl, xr, NULL, NULL, &y0);
    for(int y = y0; y < y1; y++) {
        if(xl[y] > xr[y]) continue;
        /* columns whose centre (x + 0.5) lies inside [xl, xr) */
        int x0 = clampi(iceil(xl[y] - 0.5f), 0, FB_W);
        int x1 = clampi(iceil(xr[y] - 0.5f), 0, FB_W);
        if(x1 > x0) fill_span(f->px[y], x0, x1, v, mode);
    }
}

__attribute__((optimize("O2"))) void gfx_fill_convex_shaded(Frame* f, const Vec2* pts, const float* val, int n) {
    if(n < 3) return;
    float xl[FB_H], xr[FB_H], vl[FB_H], vr[FB_H];
    int y0, y1 = scan_edges(pts, val, n, xl, xr, vl, vr, &y0);
    for(int y = y0; y < y1; y++) {
        if(xl[y] > xr[y]) continue;
        int x0 = clampi(iceil(xl[y] - 0.5f), 0, FB_W);
        int x1 = clampi(iceil(xr[y] - 0.5f), 0, FB_W);
        float dv = xr[y] > xl[y] ? (vr[y] - vl[y]) / (xr[y] - xl[y]) : 0;
        float v = vl[y] + (x0 + 0.5f - xl[y]) * dv;
        for(int x = x0; x < x1; x++, v += dv)
            f->px[y][x] = (uint8_t)clampi((int)v, 0, 255);
    }
}

void gfx_blit_mask(Frame* f, const Mask* m, int x, int y, uint8_t v) {
    if(!m) return;
    for(int my = 0; my < m->h; my++) {
        int fy = y + my;
        if((unsigned)fy >= FB_H) continue;
        const uint8_t* src = &m->bits[my * m->w];
        for(int mx = 0; mx < m->w; mx++) {
            int fx = x + mx;
            if(src[mx] && (unsigned)fx < FB_W) f->px[fy][fx] = v;
        }
    }
}

void gfx_text(Frame* f, PlatFont font, int x, int y, TextAlign align, const char* s, uint8_t v) {
    Mask* m = plat_text_mask(font, s);
    if(!m) return;
    if(align == TextCenter) x -= m->w / 2;
    if(align == TextRight) x -= m->w;
    gfx_blit_mask(f, m, x, y, v);
    plat_mask_free(m);
}
