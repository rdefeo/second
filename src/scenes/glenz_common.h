#pragma once

/* Shared by the Glenz transition and Glenz 3D: the checkerboard (palette
 * indices + palette) and the bounce that drops it into place. */

#include <stdbool.h>
#include <stdint.h>

#include "../gfx/image.h"

#define CHECKER_ZOOM_ROWS 100 /* zoomed part of the board */
#define CHECKER_EDGE_ROWS 8 /* front edge, never zoomed */

typedef struct {
    Image board; /* 128 x 111 palette indices */
    uint8_t* pal; /* 16 x (r, g, b), 6-bit */
} Checker;

bool checker_load(Checker* c);
void checker_free(Checker* c);

/* Board falling and bouncing: returns false once it has settled. `pos`
 * and `vel` are in 1/16 original rows. */
bool checker_bounce_step(int32_t* pos, int32_t* vel);

/* Original screen rows covered by the board at `pos`: zoomed part
 * [y1, y2), then the edge. */
static inline void checker_span(int32_t pos, int* y1, int* y2) {
    int y = pos / 16;
    *y1 = 130 + y / 2;
    *y2 = 130 + y * 3 / 2;
}

/* Source row of the board image shown on original screen row `y`, or -1
 * (the zoomed part only, or with the front edge too). */
int checker_row(int32_t pos, int y, bool with_edge);
