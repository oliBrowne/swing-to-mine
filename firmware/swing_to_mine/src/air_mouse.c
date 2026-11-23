#include "air_mouse.h"

#include <math.h>
#include <string.h>

void air_mouse_reset(air_mouse_t *a)
{
    memset(a, 0, sizeof(*a));
}

static void clear_queue(air_mouse_t *a)
{
    memset(a->qx, 0, sizeof(a->qx));
    memset(a->qy, 0, sizeof(a->qy));
    a->acc_x = a->acc_y = 0.0f;
}

void air_mouse_update(air_mouse_t *a, const motion_t *m, float gain, int use_pitch,
                      int hold_still, uint32_t now_ms, int16_t *dx, int16_t *dy)
{
    /* fast motion: freeze, and drop what was queued */
    if (m->gyro_mag > FREEZE_DPS) {
        if (!a->frozen) clear_queue(a);
        a->frozen = 1;
        a->calm_since = now_ms;
    } else if (a->frozen && now_ms - a->calm_since >= FREEZE_HOLD_MS) {
        a->frozen = 0;
    }

    float yaw = YAW_SIGN * vdot(m->gyro, m->up);      /* + = turning left */
    float pitch = 0.0f;
    if (use_pitch) {
        float p[3] = {0, 0, 0};
        p[PITCH_AXIS] = 1.0f;
        float k = vdot(p, m->up);
        for (int i = 0; i < 3; i++) p[i] -= k * m->up[i];
        if (vnormalize(p)) pitch = PITCH_SIGN * vdot(m->gyro, p);  /* + = tipping forward/down */
    }

    /* dead zone: subtract it (no jump at the edge) */
    float rate = sqrtf(yaw * yaw + pitch * pitch);
    float keep = 0.0f;
    if (rate > POINT_DEADZONE_DPS) keep = (rate - POINT_DEADZONE_DPS) / rate;

    int16_t sx = 0, sy = 0;
    if (!a->frozen && !hold_still) {
        a->acc_x += -yaw * keep * gain * SAMPLE_DT;    /* mouse +x = right */
        a->acc_y += pitch * keep * gain * SAMPLE_DT;   /* mouse +y = down */
        sx = (int16_t)a->acc_x;                        /* keep the fraction for next time */
        sy = (int16_t)a->acc_y;
        a->acc_x -= sx;
        a->acc_y -= sy;
    } else if (hold_still) {
        clear_queue(a);
    }

    /* 60 ms delay line: put this sample's motion in, take the oldest out */
    *dx += a->qx[a->q_head];
    *dy += a->qy[a->q_head];
    a->qx[a->q_head] = sx;
    a->qy[a->q_head] = sy;
    a->q_head = (a->q_head + 1) % POINT_DELAY_SAMPLES;
}
