/* Air mouse: how fast the board turns -> mouse movement (like a Wii remote).
 *
 *  - left/right = turning around the vertical (found from gravity), so it
 *    doesn't matter exactly how the board is held;
 *  - up/down = turning around the board's PITCH_AXIS, kept level;
 *  - a dead zone hides hand shake;
 *  - fast motion (FREEZE_DPS) freezes the camera, and throws away the last
 *    60 ms of motion that was still waiting to go out, so the start of a
 *    gesture doesn't jerk the view. Pure logic, no hardware access. */
#pragma once
#include <stdint.h>

#include "config.h"
#include "motion.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float acc_x, acc_y;                    /* fractions of a count not sent yet */
    int16_t qx[POINT_DELAY_SAMPLES], qy[POINT_DELAY_SAMPLES];
    int q_head;
    int frozen;
    uint32_t calm_since;
} air_mouse_t;

void air_mouse_reset(air_mouse_t *a);
/* One sample. gain = counts per degree, use_pitch = 0 for steering only (move
 * mode), hold_still = 1 to freeze anyway (e.g. while mining is held).
 * Adds the motion that is due now to *dx, *dy. */
void air_mouse_update(air_mouse_t *a, const motion_t *m, float gain, int use_pitch,
                      int hold_still, uint32_t now_ms, int16_t *dx, int16_t *dy);

#ifdef __cplusplus
}
#endif
