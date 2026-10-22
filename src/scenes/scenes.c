#include "scene.h"
#include "../timeline.h"

extern const Scene scene_alku;
extern const Scene scene_u2a;
extern const Scene scene_pam;
extern const Scene scene_title;
extern const Scene scene_glenz_trans;
extern const Scene scene_glenz;
extern const Scene scene_tunneli;
extern const Scene scene_techno_circles;
extern const Scene scene_techno_trans;
extern const Scene scene_techno_bars;
extern const Scene scene_troll;
extern const Scene scene_forest;
extern const Scene scene_lens_trans;
extern const Scene scene_lens;
extern const Scene scene_roto;
extern const Scene scene_plasma;
extern const Scene scene_plasma_cube;
extern const Scene scene_dots;
extern const Scene scene_water;
extern const Scene scene_coman;
extern const Scene scene_jplogo;
extern const Scene scene_u2e;
extern const Scene scene_endlogo;
extern const Scene scene_credits;
extern const Scene scene_endscrl;


const Scene* const scene_list[] = {
    &scene_alku,
    &scene_u2a,
    &scene_pam,
    &scene_title,
    &scene_glenz_trans,
    &scene_glenz,
    &scene_tunneli,
    &scene_techno_circles,
    &scene_techno_trans,
    &scene_techno_bars,
    &scene_troll,
    &scene_forest,
    &scene_lens_trans,
    &scene_lens,
    &scene_roto,
    &scene_plasma,
    &scene_plasma_cube,
    &scene_dots,
    &scene_water,
    &scene_coman,
    &scene_jplogo,
    &scene_u2e,
    &scene_endlogo,
    &scene_credits,
    &scene_endscrl,
};

const size_t scene_count = sizeof(scene_list) / sizeof(scene_list[0]);

const SceneCue scene_cue[] = {
    {SEG_S0A, SYNC_DEMO_START}, /* Alku */
    {SEG_S0A, SYNC_U2A_START}, /* U2A */
    {SEG_S0A, SYNC_PAM_START}, /* PAM */
    {CUE_AFTER, 0}, /* Title */
    {SEG_S1, 0}, /* Glenz transition */
    {SEG_S1, SYNC_GLENZ_START}, /* Glenz */
    {CUE_AFTER, 0}, /* Tunneli */
    {CUE_AFTER, 0}, /* Techno circles */
    {SEG_S1, SYNC_TECHNO_BARS_TRANSITION}, /* Techno transition */
    {CUE_AFTER, 0}, /* Techno bars */
    {SEG_S1, SYNC_TECHNO_TROLL}, /* Troll */
    {SEG_S1, SYNC_FOREST_START}, /* Forest */
    {SEG_S1, SYNC_LENS_TRANSITION_START}, /* Lens transition */
    {SEG_S1, SYNC_LENS_BOUNCE}, /* Lens */
    {SEG_S1, SYNC_LENS_ROTO}, /* Rotozoom */
    {SEG_S1, SYNC_PLZ_START}, /* Plasma */
    {SEG_S1, SYNC_PLZ_CUBE_START}, /* Plasma cube */
    {SEG_S1, SYNC_PLZ_CUBE_END}, /* Dots */
    {SEG_S1, SYNC_WATER_START}, /* Water */
    {SEG_S1, SYNC_COMAN_START}, /* Coman */
    {CUE_AFTER, 0}, /* JP logo */
    {SEG_S0B, SYNC_U2E_START}, /* U2E */
    {SEG_S0B, SYNC_U2E_END}, /* End logo */
    {SEG_S0B, SYNC_CREDITS_START}, /* Credits */
    {CUE_AFTER, 0}, /* End scroller */
};

_Static_assert(sizeof(scene_cue) / sizeof(scene_cue[0]) == sizeof(scene_list) / sizeof(scene_list[0]), "one cue per scene");

void scene_draw_error(Frame* f, const char* name) {
    gfx_clear(f, 0);
    gfx_text(f, PlatFontPrimary, FB_W / 2, 18, TextCenter, name, 255);
    gfx_text(f, PlatFontSecondary, FB_W / 2, 34, TextCenter, "failed to start", 255);
}
