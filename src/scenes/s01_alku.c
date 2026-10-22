#include "scene.h"
#include "../timeline.h"
#include "../gfx/font.h"
#include "../gfx/image.h"

#include <string.h>

/* Opening: three title cards fade in and out on black, then a landscape
 * fades in and slowly pans while four credit cards fade over it. Card
 * changes follow the music sync points. Text is lightened onto the
 * picture, like the original's OR-ed palette trick. The Dolby Surround
 * logo (a line reading "\x8f") is a picture, redrawn for our size. */

#define MAX_CARDS  8
#define MAX_LINES  4
#define FADE_TICKS 64
#define HOLD_BLACK 300 /* ticks before fade-out, black background */
#define HOLD_LAND  200 /* ... over the landscape */
#define LAND_FADE  128
#define ALKU_MS    SYNC_ALKU_EXIT
#define LOGO       '\x8f'
#define LOGO_ROWS  40 /* the original logo's height, 320x400 rows */

/* music0 sync points (ms): three cards, landscape, four cards */
static const uint32_t card_ms[MAX_CARDS] = {
    SYNC_ALKU_TEXT1, SYNC_ALKU_TEXT2, SYNC_ALKU_TEXT3, 0,
    SYNC_ALKU_TEXT4, SYNC_ALKU_TEXT5, SYNC_ALKU_TEXT6, SYNC_ALKU_TEXT7};
#define LANDSCAPE_MS SYNC_ALKU_LANDSCAPE

typedef struct {
    int16_t y;
    const char* text;
    int len;
} Line;

typedef struct {
    Font font;
    Image bg, logo;
    uint8_t* script;
    Line lines[MAX_CARDS][MAX_LINES];
    int count[MAX_CARDS];
} Alku;

/* alku.txt (from tools/convert.py): "y text" lines, cards split by a
 * blank line; card slot 3 is the landscape and has no text. */
static void parse(Alku* a, size_t size) {
    int card = 0;
    char* p = (char*)a->script;
    char* end = p + size;
    while(p < end && card < MAX_CARDS) {
        if(card == 3) card++;
        char* eol = memchr(p, '\n', end - p);
        if(!eol) eol = end;
        if(eol == p) {
            card++;
        } else if(a->count[card] < MAX_LINES) {
            Line* l = &a->lines[card][a->count[card]++];
            l->y = 0;
            char* q = p;
            while(q < eol && *q >= '0' && *q <= '9')
                l->y = (int16_t)(l->y * 10 + (*q++ - '0'));
            l->text = q + 1;
            l->len = (int)(eol - q - 1);
        }
        p = eol + 1;
    }
}

static void* alku_init(const Scene* self) {
    (void)self;
    Alku* a = plat_alloc(sizeof(Alku));
    if(!a) return NULL;
    size_t size = 0;
    if(!font_load(&a->font, "alku.srf") || !image_load(&a->bg, "alku_bg.srg") ||
       !image_load(&a->logo, "alku_logo.srg") || !(a->script = plat_load_asset("alku.txt", &size))) {
        font_free(&a->font);
        image_free(&a->bg);
        image_free(&a->logo);
        plat_free(a);
        return NULL;
    }
    parse(a, size);
    return a;
}

static int text_level(int32_t t, int hold) {
    if(t < 0) return 0;
    if(t < FADE_TICKS) return t * 256 / FADE_TICKS;
    if(t < FADE_TICKS + hold) return 256;
    t -= FADE_TICKS + hold;
    return t < FADE_TICKS ? 256 - t * 256 / FADE_TICKS : 0;
}

static void alku_render(void* state, Frame* f, uint32_t t_ms) {
    Alku* a = state;
    gfx_clear(f, 0);

    if(t_ms >= LANDSCAPE_MS) {
        int32_t lt = (int32_t)frame70(t_ms - LANDSCAPE_MS);
        /* pan 7.8 original px/s, up to 320 px (the strip is the band at
         * 0.4 per pixel, so that's up to 128 of ours) */
        float pan = (t_ms - LANDSCAPE_MS) * 7.8f / 1000;
        if(pan > 320) pan = 320;
        int x0 = clampi((int)(pan * BAND_SX), 0, a->bg.w - FB_W);
        uint8_t lut[256];
        lut_fade_black(lut, lt >= LAND_FADE ? 0 : 256 - lt * 256 / LAND_FADE);
        const uint8_t* src = image_frame(&a->bg, 0);
        for(int y = 0; y < FB_H; y++)
            for(int x = 0; x < FB_W; x++)
                f->px[y][x] = lut[src[y * a->bg.w + x0 + x]];
    }

    for(int c = 0; c < MAX_CARDS; c++) {
        if(!a->count[c] || t_ms < card_ms[c]) continue;
        uint32_t next = c + 1 < MAX_CARDS ? card_ms[c + 1 == 3 ? 4 : c + 1] : ALKU_MS;
        if(c == 2) next = LANDSCAPE_MS;
        if(t_ms >= next) continue;
        int level = text_level((int32_t)frame70(t_ms - card_ms[c]), c < 3 ? HOLD_BLACK : HOLD_LAND);
        for(int i = 0; i < a->count[c]; i++) {
            const Line* l = &a->lines[c][i];
            /* original y is in 320x400 rows: halve it, then map the band */
            int y = (int)((l->y / 2 - BAND_Y0) * BAND_SY);
            if(l->len == 1 && l->text[0] == LOGO) { /* centred where the original's was */
                uint8_t lut[256];
                lut_fade_black(lut, 256 - level);
                int cy = (int)(((l->y + LOGO_ROWS / 2) / 2.0f - BAND_Y0) * BAND_SY);
                image_draw(f, &a->logo, 0, (FB_W - a->logo.w) / 2, cy - a->logo.h / 2, lut);
            } else {
                font_draw_centered(f, &a->font, FB_W / 2, y, l->text, l->len, level);
            }
        }
    }
}

static void alku_deinit(void* state) {
    Alku* a = state;
    font_free(&a->font);
    image_free(&a->bg);
    image_free(&a->logo);
    plat_free(a->script);
    plat_free(a);
}

const Scene scene_alku = {
    .name = "Alku",
    .duration_ms = ALKU_MS,
    .init = alku_init,
    .render = alku_render,
    .deinit = alku_deinit,
};
