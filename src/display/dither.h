#pragma once

/* Temporal grayscale for the ST7567 LCD (technique from flipper-doom's
 * docs/grayscale.md, CC0; this is an independent implementation).
 *
 * Each frame is shown as `period` 1-bit subframes. A pixel with level L
 * (0..period) is white in L of them; which ones is picked by a bit-reversed
 * rank so the pulses are spread out in time, and each pixel's phase is
 * offset by a 4x4 Bayer matrix so neighbours don't blink together.
 * Intensities between levels are ordered-dithered with the same matrix.
 * With period == 1 this degenerates to a plain 4x4 ordered dither.
 *
 * The whole mapping reduces to: pixel is white in subframe s iff
 *   px >= thr[s][y & 3][x & 3]
 * so a subframe costs one compare per pixel. */

#include <stdint.h>

#include "../gfx/frame.h"

#define DITHER_MAX_PERIOD 8

typedef uint16_t DitherThresholds[4][4];

/* Valid periods are 1, 2, 3, 4, 6, 8. Returns the nearest valid one. */
uint8_t dither_valid_period(uint8_t period);
uint8_t dither_next_period(uint8_t period, int dir);

void dither_thresholds(uint8_t period, uint8_t subframe, DitherThresholds thr);

/* Write one subframe in the LCD's page layout: byte (y / 8) * 128 + x,
 * bit y & 7, bit set = black. `pages` must hold FB_W * FB_H / 8 bytes. */
void dither_subframe_pages(const Frame* f, const DitherThresholds thr, uint8_t* pages);

/* Portable version (the main one uses Cortex-M4 SIMD when available). */
void dither_subframe_pages_c(const Frame* f, const DitherThresholds thr, uint8_t* pages);
