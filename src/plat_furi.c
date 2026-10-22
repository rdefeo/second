#include "plat.h"
#include "plat_furi.h"

#include <furi.h>
#include <gui/canvas.h>
#include <storage/storage.h>
#include <stdarg.h>
#include <stdio.h>

#define TAG "Second"

static GrayDisplay* plat_display = NULL;

void plat_furi_set_display(GrayDisplay* display) {
    plat_display = display;
}

void* plat_alloc(size_t size) {
    /* furi's malloc never returns NULL (it crashes instead); check the heap
     * first so a too-large scene fails gracefully. The block must fit (with
     * the allocator's header), and 4 KB of heap must stay free elsewhere
     * for the other threads; that can be anywhere, not in this block. */
    if(size + 16 > memmgr_heap_get_max_free_block() || size + 4096 > memmgr_get_free_heap())
        return NULL;
    void* p = malloc(size);
    memset(p, 0, size);
    return p;
}

void plat_free(void* ptr) {
    free(ptr);
}

static Font font_for(PlatFont font) {
    switch(font) {
    case PlatFontPrimary:
        return FontPrimary;
    case PlatFontKeyboard:
        return FontKeyboard;
    case PlatFontBigNumbers:
        return FontBigNumbers;
    default:
        return FontSecondary;
    }
}

Mask* plat_text_mask(PlatFont font, const char* str) {
    if(!plat_display) return NULL;

    Canvas* canvas = gray_display_lock_canvas(plat_display);
    canvas_reset(canvas);
    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);
    canvas_set_font(canvas, font_for(font));
    const CanvasFontParameters* params = canvas_get_font_params(canvas, font_for(font));
    uint16_t w = canvas_string_width(canvas, str);
    uint16_t h = params->height + params->descender;
    if(w > FB_W) w = FB_W;
    if(h > FB_H) h = FB_H;
    canvas_draw_str(canvas, 0, params->height, str); /* y is the baseline */

    Mask* m = malloc(sizeof(Mask));
    m->w = w;
    m->h = h;
    m->bits = malloc((size_t)w * h + 1);
    const uint8_t* buf = canvas_get_buffer(canvas);
    for(uint16_t y = 0; y < h; y++)
        for(uint16_t x = 0; x < w; x++)
            m->bits[y * w + x] = (buf[(y >> 3) * FB_W + x] >> (y & 7)) & 1;
    gray_display_unlock_canvas(plat_display);
    return m;
}

void plat_mask_free(Mask* mask) {
    if(!mask) return;
    free(mask->bits);
    free(mask);
}

uint8_t* plat_load_asset(const char* name, size_t* size) {
    char path[96];
    snprintf(path, sizeof(path), STORAGE_APP_ASSETS_PATH_PREFIX "/%s", name);

    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* file = storage_file_alloc(storage);
    uint8_t* data = NULL;
    if(storage_file_open(file, path, FSAM_READ, FSOM_OPEN_EXISTING)) {
        size_t len = storage_file_size(file);
        data = plat_alloc(len ? len : 1);
        if(data) {
            /* Small reads keep the SPI bus (shared with the LCD) responsive. */
            size_t done = 0;
            while(done < len) {
                size_t chunk = len - done > 512 ? 512 : len - done;
                if(storage_file_read(file, data + done, chunk) != chunk) break;
                done += chunk;
            }
            if(done != len) {
                free(data);
                data = NULL;
            } else if(size) {
                *size = len;
            }
        }
    }
    if(!data) FURI_LOG_E(TAG, "asset %s failed to load", path);
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);
    return data;
}

struct PlatFile {
    Storage* storage;
    File* file;
};

PlatFile* plat_asset_open(const char* name) {
    char path[96];
    snprintf(path, sizeof(path), STORAGE_APP_ASSETS_PATH_PREFIX "/%s", name);
    PlatFile* pf = malloc(sizeof(PlatFile));
    pf->storage = furi_record_open(RECORD_STORAGE);
    pf->file = storage_file_alloc(pf->storage);
    if(!storage_file_open(pf->file, path, FSAM_READ, FSOM_OPEN_EXISTING)) {
        FURI_LOG_E(TAG, "asset %s failed to open", path);
        plat_asset_close(pf);
        return NULL;
    }
    return pf;
}

bool plat_asset_read(PlatFile* pf, uint32_t offset, void* buf, size_t len) {
    if(!storage_file_seek(pf->file, offset, true)) return false;
    uint8_t* dst = buf;
    while(len) {
        size_t chunk = len > 512 ? 512 : len;
        if(storage_file_read(pf->file, dst, chunk) != chunk) return false;
        dst += chunk;
        len -= chunk;
    }
    return true;
}

void plat_asset_close(PlatFile* pf) {
    if(!pf) return;
    storage_file_free(pf->file);
    furi_record_close(RECORD_STORAGE);
    free(pf);
}

void plat_log(const char* fmt, ...) {
    char buf[128];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    FURI_LOG_I(TAG, "%s", buf);
}
