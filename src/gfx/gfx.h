#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "frame.h"
#include "../plat.h"

/* Screen regions. Picture scenes from the 4:3 original use VIEW_43 with
 * black bars; procedural scenes use the whole 2:1 screen. */
typedef struct {
    int16_t x, y, w, h;
} Rect;

#define VIEW_FULL ((Rect){0, 0, FB_W, FB_H})
#define VIEW_43   ((Rect){21, 0, 85, FB_H})

/* Fixed point helpers. Angles are 0..FX_ANGLE_STEPS-1 for a full turn. */
#define FX_ANGLE_STEPS 1024
#define FX_ANGLE_MASK  (FX_ANGLE_STEPS - 1)
#define FX_ONE14       16384

extern const int16_t sin_lut[FX_ANGLE_STEPS]; /* Q14 */

static inline int32_t fx_sin(int32_t a) {
    return sin_lut[a & FX_ANGLE_MASK];
}

static inline int32_t fx_cos(int32_t a) {
    return sin_lut[(a + FX_ANGLE_STEPS / 4) & FX_ANGLE_MASK];
}

static inline int32_t clampi(int32_t v, int32_t lo, int32_t hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

uint32_t isqrt32(uint32_t v);

typedef enum {
    TextLeft,
    TextCenter,
    TextRight,
} TextAlign;

void gfx_clear(Frame* f, uint8_t v);
void gfx_fill_rect(Frame* f, int x, int y, int w, int h, uint8_t v);
void gfx_letterbox(Frame* f, Rect view, uint8_t v); /* fill outside view */

static inline void gfx_pset(Frame* f, int x, int y, uint8_t v) {
    if((unsigned)x < FB_W && (unsigned)y < FB_H) f->px[y][x] = v;
}

void gfx_hline(Frame* f, int x0, int x1, int y, uint8_t v);
void gfx_line(Frame* f, int x0, int y0, int x1, int y1, uint8_t v);
void gfx_disc(Frame* f, int cx, int cy, int r, uint8_t v);

/* Original timing: most parts animate at the VGA mode 13h rate of 70 Hz. */
static inline uint32_t frame70(uint32_t t_ms) {
    return t_ms * 7 / 100;
}

/* Brightness of a 6-bit VGA palette colour (0..63 per channel) as 0..255.
 * Averages luma with the max channel so saturated reds/blues from the
 * original palettes don't collapse to black on a mono screen. */
static inline uint8_t vga_lum(int r, int g, int b) {
    r = clampi(r, 0, 63);
    g = clampi(g, 0, 63);
    b = clampi(b, 0, 63);
    int luma = (r * 77 + g * 150 + b * 29) >> 8;
    int mx = r > g ? (r > b ? r : b) : (g > b ? g : b);
    return (uint8_t)(((luma + mx) * 255) / 126);
}

/* The opening scenes (Alku, U2A, PAM) are letterboxed in the original:
 * the picture is rows BAND_Y0..BAND_Y0+BAND_H-1 of 320x200 (about 2.1:1).
 * That band fills our screen, like Plasma does. */
#define BAND_Y0 25
#define BAND_H  150
#define BAND_SX ((float)FB_W / 320)
#define BAND_SY ((float)FB_H / BAND_H)

/* Map original 320x200 (4:3) coordinates onto the full 2:1 screen. */
#define ORIG_SX (FB_W / 320.0f)
#define ORIG_SY (FB_H / 200.0f)

typedef enum {
    FillSet, /* px = v */
    FillAdd, /* px = min(255, px + v) */
    FillOr, /* px |= v, for bitplane-style effects */
    FillXor, /* px ^= v */
} FillMode;

typedef struct {
    float x, y;
} Vec2;

/* Fill a convex polygon (any winding). Pixel centres inside are filled. */
void gfx_fill_convex(Frame* f, const Vec2* pts, int n, uint8_t v, FillMode mode);

/* Same, with brightness interpolated from per-vertex values (Gouraud). */
void gfx_fill_convex_shaded(Frame* f, const Vec2* pts, const float* val, int n);

/* Draw mask pixels with value v. Pixels outside the frame are clipped. */
void gfx_blit_mask(Frame* f, const Mask* m, int x, int y, uint8_t v);

/* Convenience: render + blit + free. Slow on device, fine for tests. */
void gfx_text(Frame* f, PlatFont font, int x, int y, TextAlign align, const char* s, uint8_t v);
