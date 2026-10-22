#include "font.h"

#include <string.h>

#define SRF_HEADER 8
#define SRF_TABLE  (256 * 3)

bool font_load(Font* font, const char* name) {
    memset(font, 0, sizeof(*font));
    size_t size = 0;
    uint8_t* data = plat_load_asset(name, &size);
    if(!data) return false;
    if(size < SRF_HEADER + SRF_TABLE || memcmp(data, "SRF1", 4) != 0) {
        plat_free(data);
        return false;
    }
    font->height = data[4];
    font->gap = data[5];
    font->strip_w = (uint16_t)(data[6] | data[7] << 8);
    if(size < SRF_HEADER + SRF_TABLE + (size_t)font->height * font->strip_w) {
        plat_free(data);
        memset(font, 0, sizeof(*font));
        return false;
    }
    font->blob = data;
    font->table = data + SRF_HEADER;
    font->strip = data + SRF_HEADER + SRF_TABLE;
    return true;
}

void font_free(Font* font) {
    plat_free(font->blob);
    memset(font, 0, sizeof(*font));
}

static inline void glyph(const Font* font, uint8_t c, int* x, int* w) {
    const uint8_t* e = font->table + c * 3;
    *x = e[0] | e[1] << 8;
    *w = e[2];
}

int font_width(const Font* font, const char* s, int len) {
    int total = 0;
    for(int i = 0; i < len; i++) {
        int gx, gw;
        glyph(font, (uint8_t)s[i], &gx, &gw);
        if(gw) total += gw + font->gap;
    }
    return total > 0 ? total - font->gap : 0;
}

void font_draw(Frame* f, const Font* font, int x, int y, const char* s, int len, int level) {
    if(level <= 0) return;
    for(int i = 0; i < len; i++) {
        int gx, gw;
        glyph(font, (uint8_t)s[i], &gx, &gw);
        if(!gw) continue;
        /* the space glyph points at the strip's last (blank) column but is
         * wider than what's left of the strip: draw only what exists */
        int ink = gx + gw > font->strip_w ? font->strip_w - gx : gw;
        for(int row = 0; row < font->height && ink > 0; row++) {
            int fy = y + row;
            if((unsigned)fy >= FB_H) continue;
            const uint8_t* src = font->strip + row * font->strip_w + gx;
            for(int col = 0; col < ink; col++) {
                int fx = x + col;
                if((unsigned)fx >= FB_W) continue;
                int v = (src[col] * level) >> 8;
                if(v > f->px[fy][fx]) f->px[fy][fx] = (uint8_t)v;
            }
        }
        x += gw + font->gap;
    }
}

void font_draw_centered(Frame* f, const Font* font, int cx, int y, const char* s, int len, int level) {
    font_draw(f, font, cx - font_width(font, s, len) / 2, y, s, len, level);
}
