#include "scene.h"
#include "../timeline.h"
#include "glenz_common.h"
#include "../gfx/vec3.h"

/* Into the Glenz part: the title picture waits for the music, then is
 * wiped away from top and bottom while greying out; a checkerboard then
 * drops in from above, growing as it nears, and bounces to a stop. */

#define FALL_MS      SYNC_CHECKERBOARD_FALL
#define WIPE_TICKS   48
#define TRANS_MS     SYNC_GLENZ_START

typedef struct {
    Image title;
    Checker checker;
    uint8_t lum[16];
} GlenzTrans;

static void* gtrans_init(const Scene* self) {
    (void)self;
    GlenzTrans* g = plat_alloc(sizeof(GlenzTrans));
    if(!g) return NULL;
    if(!image_load(&g->title, "title.srg") || !checker_load(&g->checker)) {
        image_free(&g->title);
        checker_free(&g->checker);
        plat_free(g);
        return NULL;
    }
    for(int i = 0; i < 16; i++) {
        const uint8_t* c = g->checker.pal + i * 3;
        g->lum[i] = vga_lum(c[0], c[1], c[2]);
    }
    return g;
}

/* Title wipe: rows from the top (and, slower, the bottom) go black while
 * the picture fades toward grey. */
static void draw_wipe(GlenzTrans* g, Frame* f, int32_t tick) {
    int zy = 0;
    for(int32_t i = 0; i <= tick && zy < 260; i++)
        zy += i / 4;
    if(zy > 260) zy = 260;
    int zy2 = 125 * zy / 260;
    int c = tick > 32 ? 32 : tick;
    int grey = vga_lum(30, 30, 30);

    const Rect v = VIEW_43;
    gfx_clear(f, 0);
    const uint8_t* src = image_frame(&g->title, 0);
    for(int y = 0; y < v.h; y++) {
        int oy = y * 400 / v.h; /* original 400-line row */
        if(oy <= zy || oy >= 399 - zy2) continue;
        for(int x = 0; x < v.w; x++)
            f->px[v.y + y][v.x + x] = (uint8_t)(((32 - c) * src[y * v.w + x] + c * grey) / 32);
    }
}

/* The board in the same 4:3 window as the title picture before it. */
static void draw_board(GlenzTrans* g, Frame* f, int32_t pos) {
    const Rect v = VIEW_43;
    gfx_clear(f, 0);
    for(int y = 0; y < v.h; y++) {
        int row = checker_row(pos, (int)(y / VGA43_SY), true);
        if(row < 0 || row >= g->checker.board.h) continue;
        /* the board image is the original's 320 columns sampled to 128 */
        const uint8_t* src = image_frame(&g->checker.board, 0) + row * g->checker.board.w;
        for(int x = 0; x < v.w; x++)
            f->px[v.y + y][v.x + x] = g->lum[src[x * g->checker.board.w / v.w] & 15];
    }
}

static void gtrans_render(void* state, Frame* f, uint32_t t_ms) {
    GlenzTrans* g = state;
    if(t_ms < FALL_MS) {
        draw_wipe(g, f, 0);
        return;
    }
    int32_t tick = (int32_t)frame70(t_ms - FALL_MS);
    if(tick <= WIPE_TICKS) {
        draw_wipe(g, f, tick);
        return;
    }
    int32_t pos = 0, vel = 0;
    for(int32_t i = WIPE_TICKS; i < tick; i++)
        if(!checker_bounce_step(&pos, &vel)) break;
    draw_board(g, f, pos);
}

static void gtrans_deinit(void* state) {
    GlenzTrans* g = state;
    image_free(&g->title);
    checker_free(&g->checker);
    plat_free(g);
}

const Scene scene_glenz_trans = {
    .name = "Glenz Transition",
    .duration_ms = TRANS_MS,
    .init = gtrans_init,
    .render = gtrans_render,
    .deinit = gtrans_deinit,
};
