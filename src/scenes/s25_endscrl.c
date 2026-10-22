#include "scene.h"
#include "../gfx/font.h"

#include <string.h>

/* End scroller: the closing text rolls up the screen, one line of the
 * original 400-line hi-res mode per 35 Hz tick, each line centred. Blank
 * lines are spacers, and a "[NN" line is a gap NN rows tall (a text line
 * is 25). Ends once the last line has left the top. */

#define MAX_LINES  768 /* the wrapped text is about 560 */
#define WRAP_W     (FB_W - 4)
#define ROWS_PER_S 35 /* original rows per second */
#define LINE_ROWS  25 /* original rows per text line */

typedef struct {
    Font font;
    uint8_t* script;
    const char* text[MAX_LINES];
    uint8_t len[MAX_LINES];
    uint8_t h[MAX_LINES]; /* px */
    int lines;
    int height; /* px, all lines */
} EndScroll;

static void add_line(EndScroll* e, const char* s, int len, int h) {
    e->text[e->lines] = s;
    e->len[e->lines] = (uint8_t)len;
    e->h[e->lines++] = (uint8_t)h;
    e->height += h;
}

static void* endscrl_init(const Scene* self) {
    (void)self;
    EndScroll* e = plat_alloc(sizeof(EndScroll));
    if(!e) return NULL;
    size_t size = 0;
    if(!font_load(&e->font, "endscrl.srf") || !(e->script = plat_load_asset("endscrl.txt", &size))) {
        font_free(&e->font);
        plat_free(e);
        return NULL;
    }
    /* lines were set for a 640 px wide screen: word-wrap them to ours */
    char* p = (char*)e->script;
    char* end = p + size;
    while(p < end && e->lines < MAX_LINES) {
        char* eol = memchr(p, '\n', end - p);
        if(!eol) eol = end;
        const char* s = p;
        int len = (int)(eol - p);
        if(len >= 2 && s[0] == '[') {
            int rows = 0;
            for(int i = 1; i < len && s[i] >= '0' && s[i] <= '9'; i++)
                rows = rows * 10 + s[i] - '0';
            add_line(e, s, 0, (rows * e->font.height + LINE_ROWS / 2) / LINE_ROWS);
            p = eol + 1;
            continue;
        }
        do {
            int take = len;
            while(take > 0 && font_width(&e->font, s, take) > WRAP_W) {
                int cut = take - 1;
                while(cut > 0 && s[cut] != ' ')
                    cut--;
                take = cut > 0 ? cut : take - 1; /* no space: hard break */
            }
            if(take <= 0) take = len;
            if(take > 255) take = 255;
            add_line(e, s, take, e->font.height);
            s += take;
            len -= take;
            while(len > 0 && *s == ' ') {
                s++;
                len--;
            }
        } while(len > 0 && e->lines < MAX_LINES);
        p = eol + 1;
    }
    return e;
}

/* pixels scrolled per second: 35 original rows, scaled like the font */
static float px_per_s(const EndScroll* e) {
    return ROWS_PER_S * e->font.height / (float)LINE_ROWS;
}

static uint32_t endscrl_duration(void* state) {
    EndScroll* e = state;
    return (uint32_t)((e->height + FB_H) * 1000 / px_per_s(e));
}

static void endscrl_render(void* state, Frame* f, uint32_t t_ms) {
    EndScroll* e = state;
    gfx_clear(f, 0);
    int scroll = (int)(t_ms * px_per_s(e) / 1000);
    int y = FB_H - scroll;
    for(int i = 0; i < e->lines && y < FB_H; y += e->h[i++])
        if(y + e->h[i] > 0) font_draw_centered(f, &e->font, FB_W / 2, y, e->text[i], e->len[i], 256);
}

static void endscrl_deinit(void* state) {
    EndScroll* e = state;
    font_free(&e->font);
    plat_free(e->script);
    plat_free(e);
}

const Scene scene_endscrl = {
    .name = "End Scroller",
    .duration_ms = 110000,
    .init = endscrl_init,
    .render = endscrl_render,
    .deinit = endscrl_deinit,
    .duration_fn = endscrl_duration,
};
