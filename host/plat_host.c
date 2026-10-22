/* Desktop implementation of plat.h for the preview harness. */
#include "../src/plat.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "font.h"

const char* host_asset_dir = "assets";

void* plat_alloc(size_t size) {
    return calloc(1, size);
}

void plat_free(void* ptr) {
    free(ptr);
}

/* All fonts map to one bitmap font on the host; good enough for layout. */
Mask* plat_text_mask(PlatFont font, const char* str) {
    (void)font;
    int w = 0;
    for(const char* c = str; *c; c++)
        if(*c >= 32 && *c < 127) w += host_font[*c - 32].w;
    if(w > 128) w = 128;
    Mask* m = malloc(sizeof(Mask));
    m->w = w;
    m->h = HOST_FONT_H;
    m->bits = calloc((size_t)w * HOST_FONT_H + 1, 1);
    int x0 = 0;
    for(const char* c = str; *c; c++) {
        if(*c < 32 || *c >= 127) continue;
        int g = *c - 32;
        for(int y = 0; y < HOST_FONT_H; y++)
            for(int x = 0; x < host_font[g].w && x0 + x < w; x++)
                m->bits[y * w + x0 + x] = (host_font[g].rows[y] >> x) & 1;
        x0 += host_font[g].w;
    }
    return m;
}

void plat_mask_free(Mask* mask) {
    if(!mask) return;
    free(mask->bits);
    free(mask);
}

uint8_t* plat_load_asset(const char* name, size_t* size) {
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", host_asset_dir, name);
    FILE* fp = fopen(path, "rb");
    if(!fp) {
        fprintf(stderr, "asset %s not found\n", path);
        return NULL;
    }
    fseek(fp, 0, SEEK_END);
    long len = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    uint8_t* data = malloc(len ? (size_t)len : 1);
    if(fread(data, 1, (size_t)len, fp) != (size_t)len) {
        free(data);
        data = NULL;
    } else if(size) {
        *size = (size_t)len;
    }
    fclose(fp);
    return data;
}

struct PlatFile {
    FILE* fp;
};

PlatFile* plat_asset_open(const char* name) {
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", host_asset_dir, name);
    FILE* fp = fopen(path, "rb");
    if(!fp) {
        fprintf(stderr, "asset %s not found\n", path);
        return NULL;
    }
    PlatFile* pf = malloc(sizeof(PlatFile));
    pf->fp = fp;
    return pf;
}

bool plat_asset_read(PlatFile* pf, uint32_t offset, void* buf, size_t len) {
    return fseek(pf->fp, (long)offset, SEEK_SET) == 0 && fread(buf, 1, len, pf->fp) == len;
}

void plat_asset_close(PlatFile* pf) {
    if(!pf) return;
    fclose(pf->fp);
    free(pf);
}

void plat_log(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fputc('\n', stderr);
}
