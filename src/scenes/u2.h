#pragma once

/* A small re-implementation of Future Crew's "U2" polygon engine, used by
 * the U2A (ships) and U2E (city) parts: objects from tools/convert.py
 * (<scene>.bin), animated by the original byte-coded stream (<scene>.anm)
 * read from the SD card, flat and Gouraud shading from one light,
 * near-plane clipping, depth-sorted objects and polygons. */

#include <stdbool.h>
#include <stdint.h>

#include "../gfx/gfx.h"
#include "../plat.h"

typedef struct U2 U2;

/* Screen mapping for U2 scenes: the original's 320x150 view window
 * (rows 25..174) fills the screen, like the rest of the opening. */
#define U2_SX BAND_SX
#define U2_SY BAND_SY

U2* u2_load(const char* scene); /* "u2a" or "u2e" */
void u2_free(U2* u2);

/* Animation frames available in the stream. */
int32_t u2_frames(const U2* u2);

/* Advance (or rewind) the animation to `frame`. */
void u2_seek(U2* u2, int32_t frame);

/* Draw the current frame over whatever is in f. */
void u2_draw(U2* u2, Frame* f);
