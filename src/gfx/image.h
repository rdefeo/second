#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "gfx.h"

/* Greyscale pictures/animations produced by tools/convert.py (.srg). */
typedef struct {
    uint16_t w, h, frames;
    uint8_t* blob; /* whole file */
    const uint8_t* px; /* first frame */
} Image;

bool image_load(Image* img, const char* name);
void image_free(Image* img);

static inline const uint8_t* image_frame(const Image* img, int frame) {
    return img->px + (uint32_t)frame * img->w * img->h;
}

/* An .srg read one frame at a time from the SD card, for animations and
 * picture sets too big to hold in RAM at once. `img` describes the frame
 * currently loaded (frames = 1). */
typedef struct {
    PlatFile* file;
    uint16_t frames;
    int loaded;
    Image img;
} ImageStream;

bool image_stream_open(ImageStream* s, const char* name);
/* Load frame n (no-op if already loaded); false on a read error. */
bool image_stream_frame(ImageStream* s, int n);
void image_stream_close(ImageStream* s);

/* Copy a frame to (x, y), clipped; lut maps brightness (NULL = as is). */
void image_draw(Frame* f, const Image* img, int frame, int x, int y, const uint8_t* lut);

/* Brightness LUTs for palette-style fades. k = 0..256. */
void lut_fade_white(uint8_t lut[256], int k); /* k=256: all white */
void lut_fade_black(uint8_t lut[256], int k); /* k=256: all black */
void lut_add(uint8_t lut[256], int add); /* brighten by add, clipped */
