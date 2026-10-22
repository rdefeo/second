#include "scene.h"
#include "../timeline.h"

void schedule_reset(Schedule* s) {
    for(int i = 0; i <= SCHEDULE_MAX; i++)
        s->zero[i] = 0;
    s->seg[SEG_S0A] = 0;
    s->seg[SEG_S1] = s->seg[SEG_S0B] = -1;
}

int32_t schedule_enter(Schedule* s, size_t idx, uint32_t duration) {
    int32_t end = s->zero[idx] + (int32_t)duration;
    if(idx == SEG_S1_AFTER_SCENE) s->seg[SEG_S1] = end;
    if(idx == SEG_S0B_AFTER_SCENE) s->seg[SEG_S0B] = end + SEG_S0B_DELAY;

    int32_t next = end;
    if(idx + 1 < scene_count) {
        const SceneCue* c = &scene_cue[idx + 1];
        if(c->seg != CUE_AFTER && s->seg[c->seg] >= 0)
            next = s->seg[c->seg] + c->ms;
        else if(c->seg == CUE_AFTER)
            next = end + c->ms;
        /* a cue that comes before this scene even started can't be met */
        if(next <= s->zero[idx]) next = end;
    }
    if(idx + 1 <= SCHEDULE_MAX) s->zero[idx + 1] = next;
    return next;
}
