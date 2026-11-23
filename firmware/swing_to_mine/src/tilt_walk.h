/* Move mode: tilt from a saved neutral angle -> W A S D, sprint, jump.
 *
 * All the move-mode rules are here and their numbers in config.h, so they are
 * easy to change. docs/controls.md ("Move mode") describes this behavior.
 *  - Entering move mode: hold still MOVE_ZERO_MS, then "up" is saved as neutral.
 *  - Forward tilt >= WALK_DEG: W. Back: S. Side >= STRAFE_DEG: A / D.
 *  - Forward >= SPRINT_DEG for SPRINT_HOLD_MS: W + Ctrl until back under WALK_DEG.
 *  - MOVE_JUMP_ENABLED: a quick upward flick taps Space.
 * Pure logic, no hardware access. */
#pragma once
#include <stdint.h>

#include "motion.h"

#ifdef __cplusplus
extern "C" {
#endif

enum { WALK_W = 1, WALK_A = 2, WALK_S = 4, WALK_D = 8, WALK_SPRINT = 16 };

typedef struct {
    int zeroed;
    uint32_t entered_ms;
    float neutral[3];
    float fwd_deg, side_deg;       /* last tilt, for debugging */
    uint8_t keys;                  /* WALK_* bits held now */
    uint32_t steep_since;          /* 0 = not steep */
    uint32_t last_jump_ms;
    int jumped_once;
} tilt_walk_t;

void tilt_walk_enter(tilt_walk_t *t, uint32_t now_ms);
/* Returns 1 on the sample where neutral was saved (blink orange once),
 * sets *jump = 1 when a jump flick is seen. */
int tilt_walk_update(tilt_walk_t *t, const motion_t *m, uint32_t now_ms, int *jump);

#ifdef __cplusplus
}
#endif
