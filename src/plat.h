#pragma once

/* Platform services used by scene and gfx code. Implemented by
 * src/plat_furi.c on the Flipper and host/plat_host.c on the desktop, so
 * scenes never include furi headers directly. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    PlatFontPrimary,
    PlatFontSecondary,
    PlatFontKeyboard,
    PlatFontBigNumbers,
} PlatFont;

/* 1 byte per pixel, nonzero = ink. */
typedef struct {
    uint16_t w;
    uint16_t h;
    uint8_t* bits;
} Mask;

void* plat_alloc(size_t size); /* zeroed; NULL on failure */
void plat_free(void* ptr);

/* Render a string with a system font into a freshly allocated mask.
 * Slow on device (borrows the LCD canvas), so call it from scene init. */
Mask* plat_text_mask(PlatFont font, const char* str);
void plat_mask_free(Mask* mask);

/* Load a whole file from the app's asset directory. Returns a plat_alloc'd
 * buffer (free with plat_free) or NULL. */
uint8_t* plat_load_asset(const char* name, size_t* size);

/* Streaming access for assets too big for RAM (e.g. animations). Reads
 * are done in small chunks so the SD card doesn't hog the LCD's SPI bus. */
typedef struct PlatFile PlatFile;
PlatFile* plat_asset_open(const char* name);
bool plat_asset_read(PlatFile* file, uint32_t offset, void* buf, size_t len);
void plat_asset_close(PlatFile* file);

void plat_log(const char* fmt, ...);
