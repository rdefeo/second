#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "gfx.h"

/* Anti-aliased bitmap fonts converted from the original demo (.srf). */
typedef struct {
    uint8_t height;
    uint8_t gap; /* px between glyphs */
    uint16_t strip_w;
    uint8_t* blob;
    const uint8_t* table; /* 256 x (u16 x, u8 w) */
    const uint8_t* strip; /* height rows of strip_w brightness values */
} Font;

bool font_load(Font* font, const char* name);
void font_free(Font* font);

/* Width of a string (bytes are glyph codes, CP437 like the originals). */
int font_width(const Font* font, const char* s, int len);

/* Draw with brightness scaled by level (0..256), blending with "lighten":
 * text only ever brightens what's under it, like the original OR-ing. */
void font_draw(Frame* f, const Font* font, int x, int y, const char* s, int len, int level);
void font_draw_centered(Frame* f, const Font* font, int cx, int y, const char* s, int len, int level);
