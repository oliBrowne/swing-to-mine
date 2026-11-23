#include "motion.h"

#include <math.h>
#include <string.h>

#include "config.h"

float vdot(const float a[3], const float b[3]) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

void vcross(const float a[3], const float b[3], float o[3])
{
    float x = a[1] * b[2] - a[2] * b[1];
    float y = a[2] * b[0] - a[0] * b[2];
    float z = a[0] * b[1] - a[1] * b[0];
    o[0] = x; o[1] = y; o[2] = z;
}

int vnormalize(float v[3])
{
    float n = sqrtf(vdot(v, v));
    if (n < 1e-6f) return 0;
    v[0] /= n; v[1] /= n; v[2] /= n;
    return 1;
}

static void block_restart(motion_t *m);

static void cal_restart(motion_t *m)
{
    m->cal_n = 0;
    for (int i = 0; i < 3; i++) {
        m->cal_sum[i] = 0.0f;
        m->cal_min[i] = 1e9f;
        m->cal_max[i] = -1e9f;
    }
}

void motion_init(motion_t *m, uint32_t now_ms)
{
    memset(m, 0, sizeof(*m));
    m->cal_start_ms = now_ms;
    m->up[UPRIGHT_AXIS] = -1.0f;   /* upright guess until the first sample */
    cal_restart(m);
    block_restart(m);
}

static void block_restart(motion_t *m)
{
    m->blk_n = 0;
    for (int i = 0; i < 3; i++) {
        m->blk_sum[i] = 0.0f;
        m->blk_min[i] = 1e9f;
        m->blk_max[i] = -1e9f;
    }
}

/* Power-on calibration: average the gyro over CAL_SAMPLES samples of keeping
 * still. Any movement starts the count again.
 *
 * A board held in a hand is hardly ever that still, so in parallel every 1 s
 * block is scored by how much the gyro moved in it, and the calmest block's
 * average is kept. After CAL_GIVE_UP_MS that average is used instead, as long
 * as the block was calm enough for a hand (CAL_FALLBACK_MAX_DPS); if the board
 * is being waved about it keeps waiting for a calmer second. It is a little
 * less exact, but the dead zone (POINT_DEADZONE_DPS) hides the rest. */
static int calibrate(motion_t *m, const float g[3], uint32_t now_ms)
{
    int still = 1;
    for (int i = 0; i < 3; i++) {
        if (g[i] < m->cal_min[i]) m->cal_min[i] = g[i];
        if (g[i] > m->cal_max[i]) m->cal_max[i] = g[i];
        m->cal_sum[i] += g[i];
        if (g[i] < m->blk_min[i]) m->blk_min[i] = g[i];
        if (g[i] > m->blk_max[i]) m->blk_max[i] = g[i];
        m->blk_sum[i] += g[i];
    }
    m->cal_n++;
    for (int i = 0; i < 3; i++)
        if (m->cal_max[i] - m->cal_min[i] > CAL_STILL_DPS) still = 0;
    if (!still) cal_restart(m);

    if (++m->blk_n >= CAL_BLOCK_SAMPLES) {
        float range = 0.0f;
        for (int i = 0; i < 3; i++)
            if (m->blk_max[i] - m->blk_min[i] > range) range = m->blk_max[i] - m->blk_min[i];
        if (!m->have_best || range < m->best_range) {
            m->have_best = 1;
            m->best_range = range;
            for (int i = 0; i < 3; i++) m->best_mean[i] = m->blk_sum[i] / (float)m->blk_n;
        }
        block_restart(m);
    }

    if (still && m->cal_n >= CAL_SAMPLES) {
        for (int i = 0; i < 3; i++) m->bias[i] = m->cal_sum[i] / (float)m->cal_n;
    } else if (m->have_best && m->best_range <= CAL_FALLBACK_MAX_DPS &&
               now_ms - m->cal_start_ms >= CAL_GIVE_UP_MS) {
        for (int i = 0; i < 3; i++) m->bias[i] = m->best_mean[i];
        m->cal_fallback = 1;
    } else {
        return 0;
    }
    m->calibrated = 1;
    return 1;
}

int motion_update(motion_t *m, const int16_t raw[6], uint32_t now_ms)
{
    float a[3], g[3];
    for (int i = 0; i < 3; i++) {
        a[i] = raw[i] / ACCEL_COUNTS_PER_G;
        g[i] = raw[3 + i] / GYRO_COUNTS_PER_DPS;
    }
    int done = 0;
    if (!m->calibrated) done = calibrate(m, g, now_ms);
    for (int i = 0; i < 3; i++) {
        m->accel[i] = a[i];
        m->gyro[i] = g[i] - m->bias[i];
    }
    m->gyro_mag = sqrtf(vdot(m->gyro, m->gyro));

    /* Gravity estimate. In the sensor's frame "up" turns opposite to the
     * board: d(up)/dt = -w x up. Then nudge it toward the accelerometer, which
     * points up when the board isn't accelerating. */
    float w[3], d[3];
    const float rad = 3.14159265f / 180.0f * SAMPLE_DT;
    for (int i = 0; i < 3; i++) w[i] = m->gyro[i] * rad;
    vcross(w, m->up, d);
    float an[3] = {a[0], a[1], a[2]};
    int a_ok = vnormalize(an);
    for (int i = 0; i < 3; i++) {
        m->up[i] -= d[i];
        if (a_ok) m->up[i] += GRAVITY_ACCEL_WEIGHT * (an[i] - m->up[i]);
    }
    vnormalize(m->up);
    if (!m->calibrated && a_ok) memcpy(m->up, an, sizeof(an));   /* still: trust the accel */
    return done;
}
