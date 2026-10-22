#include "scene.h"
#include "../timeline.h"
#include "../gfx/image.h"

/* Troll picture: slides in from the right with acceleration, lands with a
 * decaying sideways wobble and a flash, holds, then at the music sync the
 * "monitor is switched off": the picture squashes to a thin bright line,
 * the line shrinks to a dot, and the dot pulses out. */

#define SLIDE_TICKS  60
#define BOUNCE_TICKS 50
#define CRT_TICK     ((SYNC_TECHNO_TROLL_CRT - SYNC_TECHNO_TROLL) * 70 / 1000)
#define LINE_TICKS   93 /* 280 px line shrinking 3 px per tick */
#define DOT_TICKS    60
#define TROLL_MS     11600

typedef struct {
    Image img;
    int32_t squash_ticks; /* ticks for the height to go from 32 to 2 lines */
} Troll;

static void* troll_init(const Scene* self) {
    (void)self;
    Troll* t = plat_alloc(sizeof(Troll));
    if(!t) return NULL;
    if(!image_load(&t->img, "troll.srg")) {
        plat_free(t);
        return NULL;
    }
    float h = 32;
    while(h > 2) {
        h = h * 5 / 6;
        t->squash_ticks++;
    }
    return t;
}

/* Picture shifted right by `shift` original px, everything brightened. */
static void draw_shifted(Frame* f, const Image* img, float shift, int add) {
    const Rect v = VIEW_43;
    uint8_t lut[256];
    lut_add(lut, add);
    int dx = (int)(shift * v.w / 320);
    for(int y = 0; y < v.h; y++) {
        const uint8_t* src = image_frame(img, 0) + y * img->w;
        for(int x = 0; x < v.w; x++) {
            int sx = x - dx;
            f->px[v.y + y][v.x + x] = (sx >= 0 && sx < img->w) ? lut[src[sx]] : 0;
        }
    }
}

/* Whole picture squashed into `lines` original lines around the centre,
 * faded toward white by k/256. */
static void draw_squashed(Frame* f, const Image* img, float lines, int k) {
    const Rect v = VIEW_43;
    uint8_t lut[256];
    lut_fade_white(lut, k);
    float rows = lines * v.h / 200;
    if(rows < 1) rows = 1;
    int top = (int)(v.h / 2 - rows / 2);
    for(int y = 0; y < (int)(rows + 0.5f); y++) {
        int sy = clampi((int)((y + 0.5f) * img->h / rows), 0, img->h - 1);
        const uint8_t* src = image_frame(img, 0) + sy * img->w;
        for(int x = 0; x < v.w; x++)
            f->px[v.y + top + y][v.x + x] = lut[src[x]];
    }
}

static void troll_render(void* state, Frame* f, uint32_t t_ms) {
    Troll* t = state;
    int32_t tick = (int32_t)frame70(t_ms);
    gfx_clear(f, 0);

    if(tick < SLIDE_TICKS) {
        /* xpos += speed/4, speed += 1: closed form of the sum */
        float shown = tick * (tick - 1) / 8.0f;
        if(shown > 320) shown = 320;
        draw_shifted(f, &t->img, 320 - shown, 0);
    } else if(tick < CRT_TICK) {
        int32_t n = tick - SLIDE_TICKS;
        float shift = 0;
        if(n < BOUNCE_TICKS) {
            /* wobble amplitude 255/p where p grows 5/4 per tick */
            float p = 8, phase = 0;
            for(int32_t i = 0; i <= n; i++) {
                p = p > 1023 ? 1024 : p * 5 / 4;
                phase += p + 100;
            }
            shift = fx_sin((int32_t)phase) * 255.0f / FX_ONE14 / p;
        }
        int flash = 45 - 3 * clampi(n, 0, 15); /* 6-bit palette units */
        draw_shifted(f, &t->img, shift, flash * 255 / 63);
    } else {
        int32_t n = tick - CRT_TICK;
        if(n < 1) {
            draw_squashed(f, &t->img, 200, 0);
        } else if(n < 2) {
            gfx_clear(f, 255); /* white border flash */
            draw_squashed(f, &t->img, 100, 3 * 256 / 63);
        } else if(n < 3) {
            draw_squashed(f, &t->img, 100, 20 * 256 / 63);
        } else if(n < 3 + t->squash_ticks) {
            float h = 32;
            for(int32_t i = 0; i <= n - 3; i++)
                h = h * 5 / 6;
            if(h < 2) h = 2;
            draw_squashed(f, &t->img, h, (int)(256 - (h - 2) * 256 / 63));
        } else if(n < 3 + t->squash_ticks + LINE_TICKS) {
            int32_t m = n - 3 - t->squash_ticks;
            int w = (280 - 3 * m) * VIEW_43.w / 320;
            gfx_hline(f, FB_W / 2 - w / 2, FB_W / 2 + w / 2, FB_H / 2, 255);
        } else if(n < 3 + t->squash_ticks + LINE_TICKS + DOT_TICKS) {
            int32_t m = n - 3 - t->squash_ticks - LINE_TICKS;
            /* 1.5 brightness cycles over the dot's life */
            int b = 32 + fx_cos(m * FX_ANGLE_STEPS * 3 / 120) * 31 / FX_ONE14;
            gfx_pset(f, FB_W / 2, FB_H / 2, (uint8_t)(b * 255 / 63));
        }
    }
    if(tick >= SLIDE_TICKS) gfx_letterbox(f, VIEW_43, tick >= CRT_TICK + 1 && tick < CRT_TICK + 2 ? 255 : 0);
}

static void troll_deinit(void* state) {
    Troll* t = state;
    image_free(&t->img);
    plat_free(t);
}

const Scene scene_troll = {
    .name = "Troll",
    .duration_ms = TROLL_MS,
    .init = troll_init,
    .render = troll_render,
    .deinit = troll_deinit,
};
