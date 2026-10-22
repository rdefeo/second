/* Desktop preview harness: renders scenes exactly as the Flipper would
 * (same scene code, same dither thresholds) and dumps what the LCD would
 * show, averaged over the subframes, as raw 128x64 frames for to_gif.py.
 *
 *   sr_host [-p period] [-f fps] [-s max_seconds] [-o dir] scene...
 *   scene: 1..25 (menu numbers), 0 = test pattern, or "all"
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/display/dither.h"
#include "../src/scenes/scene.h"

extern const char* host_asset_dir;

static void perceive(const Frame* in, uint8_t period, Frame* out) {
    static uint8_t pages[FB_W * FB_H / 8];
    static uint16_t sum[FB_H][FB_W];
    DitherThresholds thr;
    memset(sum, 0, sizeof(sum));
    for(uint8_t s = 0; s < period; s++) {
        dither_thresholds(period, s, thr);
        dither_subframe_pages(in, thr, pages);
        for(int y = 0; y < FB_H; y++)
            for(int x = 0; x < FB_W; x++)
                if(!((pages[(y >> 3) * FB_W + x] >> (y & 7)) & 1)) sum[y][x]++;
    }
    for(int y = 0; y < FB_H; y++)
        for(int x = 0; x < FB_W; x++)
            out->px[y][x] = (uint8_t)(sum[y][x] * 255 / period);
}

static int render(const Scene* sc, const char* tag, int period, int fps, int max_s, const char* dir) {
    void* st = sc->init(sc);
    if(!st) {
        fprintf(stderr, "%s: init failed\n", sc->name);
        return 1;
    }
    char path[512];
    snprintf(path, sizeof(path), "%s/%s.raw", dir, tag);
    FILE* fp = fopen(path, "wb");
    if(!fp) {
        perror(path);
        return 1;
    }
    uint32_t dur = scene_duration(sc, st);
    if(dur > (uint32_t)max_s * 1000) dur = (uint32_t)max_s * 1000;
    static Frame f, shown;
    memset(&f, 0, sizeof(f));
    int n = 0;
    for(uint32_t t = 0; t < dur; t = (uint32_t)((uint64_t)++n * 1000 / fps)) {
        sc->render(st, &f, t);
        perceive(&f, (uint8_t)period, &shown);
        fwrite(&shown, sizeof(shown), 1, fp);
    }
    fclose(fp);
    sc->deinit(st);
    printf("%s %d %d %s\n", path, n, fps, sc->name);
    return 0;
}

/* Print the Play All timeline (scene starts/ends on the global clock). */
static int print_schedule(void) {
    Schedule sched;
    schedule_reset(&sched);
    for(size_t i = 0; i < scene_count; i++) {
        void* st = scene_list[i]->init(scene_list[i]);
        uint32_t dur = scene_duration(scene_list[i], st);
        int32_t zero = sched.zero[i];
        int32_t next = schedule_enter(&sched, i, dur);
        int32_t hold = next - zero - (int32_t)dur;
        printf(
            "%2u %-18s %3ld:%06.3f  len %7.3f  %s %6.3f s\n",
            (unsigned)(i + 1),
            scene_list[i]->name,
            (long)(zero / 60000),
            (zero % 60000) / 1000.0,
            dur / 1000.0,
            hold >= 0 ? "hold" : "CUT ",
            (hold >= 0 ? hold : -hold) / 1000.0);
        if(st) scene_list[i]->deinit(st);
    }
    int32_t end = sched.zero[scene_count];
    printf("end %ld:%06.3f\n", (long)(end / 60000), (end % 60000) / 1000.0);
    return 0;
}

int main(int argc, char** argv) {
    int period = 4, fps = 25, max_s = 20;
    const char* dir = "out";
    int i = 1;
    for(; i < argc && argv[i][0] == '-'; i += 2) {
        if(i + 1 >= argc) break;
        if(!strcmp(argv[i], "-p")) period = dither_valid_period(atoi(argv[i + 1]));
        if(!strcmp(argv[i], "-f")) fps = atoi(argv[i + 1]);
        if(!strcmp(argv[i], "-s")) max_s = atoi(argv[i + 1]);
        if(!strcmp(argv[i], "-o")) dir = argv[i + 1];
        if(!strcmp(argv[i], "-a")) host_asset_dir = argv[i + 1];
        if(!strcmp(argv[i], "-t")) {
            host_asset_dir = argv[i + 1];
            return print_schedule();
        }
    }
    if(i >= argc) {
        fprintf(stderr, "usage: %s [-p period] [-f fps] [-s secs] [-o dir] [-a assets] scene|all...\n", argv[0]);
        return 2;
    }
    int rc = 0;
    for(; i < argc; i++) {
        char tag[16];
        if(!strcmp(argv[i], "all")) {
            for(size_t k = 0; k < scene_count; k++) {
                snprintf(tag, sizeof(tag), "s%02u", (unsigned)(k + 1));
                rc |= render(scene_list[k], tag, period, fps, max_s, dir);
            }
            continue;
        }
        int k = atoi(argv[i]);
        snprintf(tag, sizeof(tag), "s%02d", k);
        if(k == 0)
            rc |= render(&scene_test_pattern, tag, period, fps, max_s, dir);
        else if(k >= 1 && (size_t)k <= scene_count)
            rc |= render(scene_list[k - 1], tag, period, fps, max_s, dir);
        else
            fprintf(stderr, "no scene %s\n", argv[i]), rc = 1;
    }
    return rc;
}
