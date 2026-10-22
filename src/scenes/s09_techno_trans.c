#include "scene.h"
#include "../timeline.h"

/* Transition into the techno bars: flash to white, then on four beats a
 * quarter-screen column fades from white to pale blue and gets wiped to
 * black from the top with an accelerating edge. One last flash ends it. */

#define BEAT_MS       (SYNC_TECHNO_BAR2 - SYNC_TECHNO_BAR1)
#define FIRST_BAR_MS  (SYNC_TECHNO_BAR1 - SYNC_TECHNO_BARS_TRANSITION)
#define FINAL_MS      (SYNC_TECHNO_BAR_FINAL_FLASH - SYNC_TECHNO_BARS_TRANSITION)
#define TRANS_MS      (FINAL_MS + 4 * 1000 / 70)
#define FLASH_IN_MS   (4 * 1000 / 70)
#define FLASH_OUT_MS  (8 * 1000 / 70)

static void* trans_init(const Scene* self) {
    (void)self;
    return (void*)1;
}

/* rows wiped after n ticks: 1, 3, 6, 10, ... (accelerating), of 200 */
static int wiped_rows(int32_t n) {
    int32_t lines = (n + 1) * (n + 2) / 2;
    if(lines > 200) lines = 200;
    return lines * FB_H / 200;
}

static void trans_render(void* state, Frame* f, uint32_t t_ms) {
    (void)state;
    const uint8_t blue = vga_lum(45, 52, 60);
    int flash; /* 0..256 toward white */

    if(t_ms < FIRST_BAR_MS) {
        flash = clampi((int)(t_ms * 256 / FLASH_IN_MS), 0, 256);
        gfx_clear(f, 0);
    } else {
        int bar = clampi((int)((t_ms - FIRST_BAR_MS) / BEAT_MS), 0, 3);
        uint32_t since = t_ms - FIRST_BAR_MS - bar * BEAT_MS;
        flash = 256 - clampi((int)(since * 256 / FLASH_OUT_MS), 0, 256);
        gfx_clear(f, blue);
        for(int b = 0; b <= bar; b++) {
            int rows = b < bar ? FB_H : wiped_rows((int32_t)frame70(since));
            gfx_fill_rect(f, b * FB_W / 4, 0, FB_W / 4, rows, 0);
        }
    }
    if(t_ms >= FINAL_MS) flash = clampi((int)((t_ms - FINAL_MS) * 256 / FLASH_IN_MS), 0, 256);

    if(flash > 0) {
        for(int y = 0; y < FB_H; y++)
            for(int x = 0; x < FB_W; x++)
                f->px[y][x] = (uint8_t)((f->px[y][x] * (256 - flash) + 255 * flash) >> 8);
    }
}

static void trans_deinit(void* state) {
    (void)state;
}

const Scene scene_techno_trans = {
    .name = "Techno Transition",
    .duration_ms = TRANS_MS,
    .init = trans_init,
    .render = trans_render,
    .deinit = trans_deinit,
};
