#pragma once

#include <stdint.h>

#define FB_W 128
#define FB_H 64

/* One byte per pixel: 0 = black, 255 = white. The display layer turns this
 * into 1-bit subframes (temporal grayscale or plain ordered dither). */
typedef struct {
    uint8_t px[FB_H][FB_W];
} Frame;
