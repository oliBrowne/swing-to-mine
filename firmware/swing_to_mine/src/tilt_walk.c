#include "tilt_walk.h"

#include <math.h>
#include <string.h>

#include "config.h"

void tilt_walk_enter(tilt_walk_t *t, uint32_t now_ms)
{
    memset(t, 0, sizeof(*t));
    t->entered_ms = now_ms;
}

/* hold a key with a little hysteresis: on at `on`, off below on - TILT_HYST_DEG */
static int hyst(int held, float v, float on)
{
    return held ? (v > on - TILT_HYST_DEG) : (v >= on);
}

int tilt_walk_update(tilt_walk_t *t, const motion_t *m, uint32_t now_ms, int *jump)
{
    *jump = 0;
    if (!t->zeroed) {
        if (now_ms - t->entered_ms < MOVE_ZERO_MS) return 0;
        memcpy(t->neutral, m->up, sizeof(t->neutral));
        t->zeroed = 1;
        return 1;
    }

    /* Tilt angles of "up" against neutral, around two level axes:
     * p = the pitch axis (forward tilt), s = n x p (sideways tilt).
     * Turning the board by +a around p turns "up" by -a in the sensor's frame,
     * so (up x n) . p = sin(a). */
    const float *n = t->neutral;
    float p[3] = {0, 0, 0}, s[3], c[3];
    p[PITCH_AXIS] = 1.0f;
    float k = vdot(p, n);
    for (int i = 0; i < 3; i++) p[i] -= k * n[i];
    vnormalize(p);
    vcross(n, p, s);
    vcross(m->up, n, c);
    const float deg = 180.0f / 3.14159265f;
    float cosang = vdot(m->up, n);
    float fwd = PITCH_SIGN * atan2f(vdot(c, p), cosang) * deg;
    float side = SIDE_SIGN * atan2f(vdot(c, s), cosang) * deg;
    t->fwd_deg = fwd;
    t->side_deg = side;

    uint8_t keys = 0;
    if (hyst(t->keys & WALK_W, fwd, WALK_DEG)) keys |= WALK_W;
    if (hyst(t->keys & WALK_S, -fwd, WALK_DEG)) keys |= WALK_S;
    if (hyst(t->keys & WALK_D, side, STRAFE_DEG)) keys |= WALK_D;
    if (hyst(t->keys & WALK_A, -side, STRAFE_DEG)) keys |= WALK_A;

    /* sprint: steep lean held a moment, then stays on until W lets go */
    if (fwd >= SPRINT_DEG) {
        if (!t->steep_since) t->steep_since = now_ms ? now_ms : 1;
    } else {
        t->steep_since = 0;
    }
    if (!(keys & WALK_W)) {
        keys &= (uint8_t)~WALK_SPRINT;
    } else if ((t->keys & WALK_SPRINT) ||
               (t->steep_since && now_ms - t->steep_since >= SPRINT_HOLD_MS)) {
        keys |= WALK_SPRINT;
    }
    t->keys = keys;

#if MOVE_JUMP_ENABLED
    /* jump: acceleration along "up" well beyond gravity */
    float up_g = vdot(m->accel, m->up) - 1.0f;
    if (up_g > JUMP_UP_G && (!t->jumped_once || now_ms - t->last_jump_ms >= JUMP_COOLDOWN_MS)) {
        t->last_jump_ms = now_ms;
        t->jumped_once = 1;
        *jump = 1;
    }
#endif
    return 0;
}
