#pragma once

#include "display/gray.h"

/* Display used by plat_text_mask(); set while the runner owns the screen. */
void plat_furi_set_display(GrayDisplay* display);
