#include "image.h"

#include <string.h>

#define SRG_HEADER 12

bool image_load(Image* img, const char* name) {
    memset(img, 0, sizeof(*img));
    size_t size = 0;
    uint8_t* data = plat_load_asset(name, &size);
    if(!data) return false;
    if(size < SRG_HEADER || memcmp(data, "SRG1", 4) != 0) {
        plat_free(data);
        return false;
    }
    img->w = (uint16_t)(data[4] | data[5] << 8);
    img->h = (uint16_t)(data[6] | data[7] << 8);
    img->frames = (uint16_t)(data[8] | data[9] << 8);
    if(size < SRG_HEADER + (size_t)img->w * img->h * img->frames) {
        plat_free(data);
        memset(img, 0, sizeof(*img));
        return false;
    }
    img->blob = data;
    img->px = data + SRG_HEADER;
    return true;
}

bool image_stream_open(ImageStream* s, const char* name) {
    memset(s, 0, sizeof(*s));
    uint8_t hdr[SRG_HEADER];
    s->file = plat_asset_open(name);
    if(!s->file || !plat_asset_read(s->file, 0, hdr, sizeof(hdr)) || memcmp(hdr, "SRG1", 4) != 0) {
        image_stream_close(s);
        return false;
    }
    s->img.w = (uint16_t)(hdr[4] | hdr[5] << 8);
    s->img.h = (uint16_t)(hdr[6] | hdr[7] << 8);
    s->frames = (uint16_t)(hdr[8] | hdr[9] << 8);
    s->img.frames = 1;
    s->img.blob = plat_alloc((size_t)s->img.w * s->img.h);
    s->img.px = s->img.blob;
    s->loaded = -1;
    if(!s->img.blob) {
        image_stream_close(s);
        return false;
    }
    return true;
}

bool image_stream_frame(ImageStream* s, int n) {
    if(n == s->loaded) return true;
    uint32_t size = (uint32_t)s->img.w * s->img.h;
    if(n < 0 || n >= s->frames || !plat_asset_read(s->file, SRG_HEADER + n * size, s->img.blob, size))
        return false;
    s->loaded = n;
    return true;
}

void image_stream_close(ImageStream* s) {
    plat_asset_close(s->file);
    plat_free(s->img.blob);
    memset(s, 0, sizeof(*s));
}

void image_free(Image* img) {
    plat_free(img->blob);
    memset(img, 0, sizeof(*img));
}

void image_draw(Frame* f, const Image* img, int frame, int x, int y, const uint8_t* lut) {
    const uint8_t* src = image_frame(img, frame);
    int x0 = x < 0 ? -x : 0;
    int x1 = x + img->w > FB_W ? FB_W - x : img->w;
    if(x1 <= x0) return;
    for(int sy = 0; sy < img->h; sy++) {
        int dy = y + sy;
        if((unsigned)dy >= FB_H) continue;
        const uint8_t* s = src + sy * img->w;
        uint8_t* row = f->px[dy];
        if(lut) {
            for(int sx = x0; sx < x1; sx++)
                row[x + sx] = lut[s[sx]];
        } else {
            memcpy(row + x + x0, s + x0, x1 - x0);
        }
    }
}

void lut_fade_white(uint8_t lut[256], int k) {
    for(int i = 0; i < 256; i++)
        lut[i] = (uint8_t)(i + (((255 - i) * k) >> 8));
}

void lut_fade_black(uint8_t lut[256], int k) {
    for(int i = 0; i < 256; i++)
        lut[i] = (uint8_t)((i * (256 - k)) >> 8);
}

void lut_add(uint8_t lut[256], int add) {
    for(int i = 0; i < 256; i++)
        lut[i] = (uint8_t)clampi(i + add, 0, 255);
}
