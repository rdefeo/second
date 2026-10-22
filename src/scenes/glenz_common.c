#include "glenz_common.h"

#include "../plat.h"

bool checker_load(Checker* c) {
    size_t size = 0;
    c->pal = NULL;
    if(!image_load(&c->board, "checker.srg")) return false;
    c->pal = plat_load_asset("checker.pal", &size);
    if(!c->pal || size < 48) {
        checker_free(c);
        return false;
    }
    /* The board's colours are dim (at most ~27 of 63) and nearly vanish on
     * the LCD: double them, capped at full brightness. */
    for(int i = 0; i < 48; i++)
        c->pal[i] = (uint8_t)(c->pal[i] * 2 > 63 ? 63 : c->pal[i] * 2);
    return true;
}

void checker_free(Checker* c) {
    image_free(&c->board);
    plat_free(c->pal);
    c->pal = NULL;
}

bool checker_bounce_step(int32_t* pos, int32_t* vel) {
    (*vel)++;
    *pos += *vel;
    if(*pos > 48 * 16) { /* hit the floor: bounce back with 2/3 speed */
        *pos -= *vel;
        *vel = -*vel * 2 / 3;
        if(*vel > -4 && *vel < 4) return false;
    }
    return true;
}

int checker_row(int32_t pos, int y, bool with_edge) {
    int y1, y2;
    checker_span(pos, &y1, &y2);
    if(y >= y1 && y < y2) return (y - y1) * CHECKER_ZOOM_ROWS / (y2 - y1);
    if(with_edge && y >= y2 && y < y2 + CHECKER_EDGE_ROWS) return CHECKER_ZOOM_ROWS + (y - y2);
    return -1;
}
