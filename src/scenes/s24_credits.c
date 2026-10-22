#include "scene.h"
#include "../gfx/font.h"
#include "../gfx/image.h"

#include <string.h>

/* Credits: for each part of the demo a small picture slides in from the
 * right while its credit lines rise from below; they hold, then the
 * picture leaves to the left and the text drops away. Laid out for 2:1:
 * picture on top, up to four lines of text under it. */

#define MAX_SCREENS 24
#define MAX_LINES   4
#define MAX_STEPS   512
#define LINE_PITCH  8
#define PIC_X       44 /* final picture position (40x30 picture) */
#define TEXT_Y      31

typedef struct {
    const char* text;
    int len;
} Line;

typedef struct {
    ImageStream pics; /* one 40x30 picture loaded at a time */
    Font font;
    uint8_t* script;
    Line lines[MAX_SCREENS][MAX_LINES];
    int count[MAX_SCREENS];
    int screens;
    /* per-tick motion of one screen, original units */
    int16_t text_y[MAX_STEPS], pic_x[MAX_STEPS];
    int steps;
} Credits;

static void add_step(Credits* c, int text_y, int pic_x) {
    if(c->steps >= MAX_STEPS) return;
    c->text_y[c->steps] = (int16_t)text_y;
    c->pic_x[c->steps] = (int16_t)pic_x;
    c->steps++;
}

static void build_motion(Credits* c) {
    int32_t y;
    for(int i = 0; i < 5; i++) /* short pause off screen */
        add_step(c, 200, 322);
    for(y = 200 * 128; y > 0; y = y * 12 / 13) /* ease in */
        add_step(c, y / 128, 80 + y / 106);
    for(int i = 0; i < 200; i++) /* hold */
        add_step(c, 0, 80);
    for(int32_t v = 0, yy = 0; yy < 128 * 200; yy += v, v += 15) /* accelerate out */
        add_step(c, yy / 128, 80 - yy / 106);
}

/* credits.txt: one line per credit, screens separated by a blank line */
static void parse(Credits* c, size_t size) {
    char* p = (char*)c->script;
    char* end = p + size;
    int s = 0;
    while(p < end && s < MAX_SCREENS) {
        char* eol = memchr(p, '\n', end - p);
        if(!eol) eol = end;
        if(eol == p) {
            s++;
        } else if(c->count[s] < MAX_LINES) {
            c->lines[s][c->count[s]++] = (Line){p, (int)(eol - p)};
        }
        p = eol + 1;
    }
    c->screens = s + (c->count[s] ? 1 : 0);
}

static void* credits_init(const Scene* self) {
    (void)self;
    Credits* c = plat_alloc(sizeof(Credits));
    if(!c) return NULL;
    size_t size = 0;
    if(!image_stream_open(&c->pics, "credits.srg") || !font_load(&c->font, "credits.srf") ||
       !(c->script = plat_load_asset("credits.txt", &size))) {
        image_stream_close(&c->pics);
        font_free(&c->font);
        plat_free(c);
        return NULL;
    }
    parse(c, size);
    if(c->screens > c->pics.frames) c->screens = c->pics.frames;
    build_motion(c);
    return c;
}

static uint32_t credits_duration(void* state) {
    Credits* c = state;
    return (uint32_t)c->screens * c->steps * 1000 / 70;
}

static void credits_render(void* state, Frame* f, uint32_t t_ms) {
    Credits* c = state;
    gfx_clear(f, 0);
    int32_t tick = (int32_t)frame70(t_ms);
    int screen = tick / c->steps, step = tick % c->steps;
    if(screen >= c->screens) return;

    int px = PIC_X + (c->pic_x[step] - 80) * 2 / 5;
    if(image_stream_frame(&c->pics, screen)) image_draw(f, &c->pics.img, 0, px, 0, NULL);
    int ty = TEXT_Y + c->text_y[step] * 3 / 20;
    for(int i = 0; i < c->count[screen]; i++) {
        const Line* l = &c->lines[screen][i];
        font_draw_centered(f, &c->font, FB_W / 2, ty + i * LINE_PITCH, l->text, l->len, 256);
    }
}

static void credits_deinit(void* state) {
    Credits* c = state;
    image_stream_close(&c->pics);
    font_free(&c->font);
    plat_free(c->script);
    plat_free(c);
}

const Scene scene_credits = {
    .name = "Credits",
    .duration_ms = 113000,
    .init = credits_init,
    .render = credits_render,
    .deinit = credits_deinit,
    .duration_fn = credits_duration,
};
