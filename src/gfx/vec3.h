#pragma once

#include "gfx.h"

/* 4:3 scenes that follow a 4:3 picture (the Glenz part after the title):
 * the whole 320x200 original in the 85x64 window VIEW_43, black bars on
 * the sides. */
#define VGA43_SX ((float)85 / 320)
#define VGA43_SY ((float)FB_H / 200)

static inline Vec2 vga43(float x, float y) {
    return (Vec2){21 + x * VGA43_SX, y * VGA43_SY};
}

typedef struct {
    float x, y, z;
} Vec3;

/* Angles in tenths of a degree (3600 per turn), as in the original. */
static inline float sin10(int32_t a) {
    return fx_sin(a * FX_ANGLE_STEPS / 3600) / (float)FX_ONE14;
}

static inline float cos10(int32_t a) {
    return fx_cos(a * FX_ANGLE_STEPS / 3600) / (float)FX_ONE14;
}

/* m = Ry * Rx * Rz */
static inline void mat_rot_yxz(float m[9], int32_t ry, int32_t rx, int32_t rz) {
    float sx = sin10(rx), cx = cos10(rx), sy = sin10(ry), cy = cos10(ry);
    float sz = sin10(rz), cz = cos10(rz);
    m[0] = cy * cz + sy * sx * sz;
    m[1] = -cy * sz + sy * sx * cz;
    m[2] = sy * cx;
    m[3] = cx * sz;
    m[4] = cx * cz;
    m[5] = -sx;
    m[6] = -sy * cz + cy * sx * sz;
    m[7] = sy * sz + cy * sx * cz;
    m[8] = cy * cx;
}

static inline Vec3 mat_apply(const float m[9], Vec3 v) {
    return (Vec3){
        m[0] * v.x + m[1] * v.y + m[2] * v.z,
        m[3] * v.x + m[4] * v.y + m[5] * v.z,
        m[6] * v.x + m[7] * v.y + m[8] * v.z};
}
