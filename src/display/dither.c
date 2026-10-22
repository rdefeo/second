#include "dither.h"

#include <string.h>

static const uint8_t bayer4[4][4] = {
    {0, 8, 2, 10},
    {12, 4, 14, 6},
    {3, 11, 1, 9},
    {15, 7, 13, 5},
};

/* rank[p] = order in which phase p turns white as the level rises. Low
 * levels light phases that are far apart, so flicker stays high-frequency. */
static const uint8_t rank1[] = {0};
static const uint8_t rank2[] = {0, 1};
static const uint8_t rank3[] = {0, 1, 2};
static const uint8_t rank4[] = {0, 2, 1, 3};
static const uint8_t rank6[] = {0, 3, 1, 4, 2, 5};
static const uint8_t rank8[] = {0, 4, 2, 6, 1, 5, 3, 7};

static const uint8_t* rank_for(uint8_t period) {
    switch(period) {
    case 2:
        return rank2;
    case 3:
        return rank3;
    case 4:
        return rank4;
    case 6:
        return rank6;
    case 8:
        return rank8;
    default:
        return rank1;
    }
}

static const uint8_t valid_periods[] = {1, 2, 3, 4, 6, 8};
#define VALID_PERIOD_COUNT (sizeof(valid_periods) / sizeof(valid_periods[0]))

uint8_t dither_valid_period(uint8_t period) {
    uint8_t best = valid_periods[0];
    for(unsigned i = 0; i < VALID_PERIOD_COUNT; i++)
        if(valid_periods[i] <= period) best = valid_periods[i];
    return best;
}

uint8_t dither_next_period(uint8_t period, int dir) {
    period = dither_valid_period(period);
    for(unsigned i = 0; i < VALID_PERIOD_COUNT; i++) {
        if(valid_periods[i] != period) continue;
        int j = (int)i + (dir < 0 ? -1 : 1);
        if(j < 0) j = 0;
        if(j >= (int)VALID_PERIOD_COUNT) j = VALID_PERIOD_COUNT - 1;
        return valid_periods[j];
    }
    return period;
}

void dither_thresholds(uint8_t period, uint8_t subframe, DitherThresholds thr) {
    period = dither_valid_period(period);
    const uint8_t* rank = rank_for(period);
    for(int y = 0; y < 4; y++) {
        for(int x = 0; x < 4; x++) {
            uint8_t b = bayer4[y][x];
            uint8_t phase = (subframe + b * period / 16) % period;
            /* Level E = floor((px * P + d) / 256); white iff rank < E. */
            int32_t d = b * 16 + 8;
            int32_t num = 256 * (rank[phase] + 1) - d;
            int32_t t = (num + period - 1) / period;
            if(t < 0) t = 0;
            thr[y][x] = (uint16_t)t;
        }
    }
}

#if defined(__ARM_FEATURE_SIMD32)
#include <arm_acle.h>

/* Cortex-M4 DSP SIMD: four pixels per instruction. USUB8 sets one GE flag
 * per byte where pixel >= threshold (thresholds are always 1..255, so they
 * fit in a byte); SEL turns the flags into a byte mask. The four pixels
 * of a word land in four adjacent output bytes (same bit), so a page of
 * 8 rows is assembled in a register and stored once. */
__attribute__((optimize("O2"))) void
    dither_subframe_pages(const Frame* f, const DitherThresholds thr, uint8_t* pages) {
    if(((uintptr_t)pages & 3) || ((uintptr_t)f->px & 3)) {
        dither_subframe_pages_c(f, thr, pages); /* not word aligned */
        return;
    }
    uint32_t tw[4];
    for(int r = 0; r < 4; r++)
        tw[r] = thr[r][0] | thr[r][1] << 8 | thr[r][2] << 16 | (uint32_t)thr[r][3] << 24;
    uint32_t* out = (uint32_t*)pages;
    for(int page = 0; page < FB_H / 8; page++) {
        const uint32_t* rows[8];
        for(int b = 0; b < 8; b++)
            rows[b] = (const uint32_t*)f->px[page * 8 + b];
        for(int x = 0; x < FB_W / 4; x++) {
            uint32_t black = 0;
            for(int b = 0; b < 8; b++) {
                __usub8(rows[b][x], tw[b & 3]);
                black |= ~__sel(0xffffffffu, 0) & (0x01010101u << b);
            }
            out[page * (FB_W / 4) + x] = black;
        }
    }
}

void dither_subframe_pages_c(const Frame* f, const DitherThresholds thr, uint8_t* pages) {
#else
void dither_subframe_pages(const Frame* f, const DitherThresholds thr, uint8_t* pages) {
    dither_subframe_pages_c(f, thr, pages);
}

void dither_subframe_pages_c(const Frame* f, const DitherThresholds thr, uint8_t* pages) {
#endif
    memset(pages, 0, FB_W * FB_H / 8);
    for(int y = 0; y < FB_H; y++) {
        const uint8_t* row = f->px[y];
        uint8_t* out = pages + (y >> 3) * FB_W;
        const uint8_t sh = y & 7;
        const uint16_t t0 = thr[y & 3][0], t1 = thr[y & 3][1];
        const uint16_t t2 = thr[y & 3][2], t3 = thr[y & 3][3];
        for(int x = 0; x < FB_W; x += 4) {
            out[x + 0] |= (uint8_t)((row[x + 0] < t0) << sh);
            out[x + 1] |= (uint8_t)((row[x + 1] < t1) << sh);
            out[x + 2] |= (uint8_t)((row[x + 2] < t2) << sh);
            out[x + 3] |= (uint8_t)((row[x + 3] < t3) << sh);
        }
    }
}
