#pragma once

#include <gui/gui.h>
#include <stdbool.h>
#include <stdint.h>

#include "../gfx/frame.h"

typedef struct {
    uint16_t rate10; /* subframe rate in 0.1 Hz; must match the panel (~75 Hz) */
    uint8_t period; /* subframes per frame: 1 = plain dither, 4 = 5 gray levels */
    uint8_t volume; /* Play All music volume, 1..GRAY_VOLUME_MAX */
} GrayConfig;

#define GRAY_RATE10_DEFAULT 750
#define GRAY_RATE10_MIN     500
#define GRAY_RATE10_MAX     1000
#define GRAY_PERIOD_DEFAULT 4
#define GRAY_VOLUME_MAX     8

void gray_config_load(GrayConfig* cfg);
bool gray_config_save(const GrayConfig* cfg);

typedef struct GrayDisplay GrayDisplay;

/* Takes over the screen (gui direct draw) and starts the subframe thread. */
GrayDisplay* gray_display_start(Gui* gui, const GrayConfig* cfg);
void gray_display_stop(GrayDisplay* d);

void gray_display_set_config(GrayDisplay* d, const GrayConfig* cfg);

/* Frame to render into. Its contents persist between presents. */
Frame* gray_display_frame(GrayDisplay* d);
/* Publish the rendered frame to the subframe thread. */
void gray_display_present(GrayDisplay* d);

/* Borrow the canvas between subframes (used for rendering font text).
 * Anything drawn is overwritten by the next subframe. */
Canvas* gray_display_lock_canvas(GrayDisplay* d);
void gray_display_unlock_canvas(GrayDisplay* d);

/* Result of the startup check of the SIMD subframe builder against the
 * portable one: 1 = identical, 0 = mismatch, -1 = not run. */
int gray_display_simd_check(void);

/* Subframes that missed their deadline by more than a full period. */
uint32_t gray_display_late_count(GrayDisplay* d);
