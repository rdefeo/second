#pragma once

#include <gui/gui.h>
#include <stddef.h>

#include "display/gray.h"

typedef enum {
    RunnerModeSingle, /* loop one scene; Left/Right switch scenes */
    RunnerModePlayAll, /* play from `start` to the end, then return */
    RunnerModeSetup, /* test pattern with rate/period controls */
} RunnerMode;

/* Takes over the screen until the user presses Back (or Play All ends).
 * In setup mode `cfg` is updated and saved. */
void runner_run(Gui* gui, GrayConfig* cfg, RunnerMode mode, size_t start);
