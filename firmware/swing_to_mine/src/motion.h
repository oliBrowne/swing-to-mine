/* Turns raw MPU-6050 counts into what the controller needs:
 *  - gyro in deg/s with the power-on bias removed,
 *  - accel in g,
 *  - "up" as a unit vector in the sensor's frame (gravity estimate that the
 *    gyro keeps steady during fast motion and the accelerometer slowly corrects).
 * Pure logic: no hardware access, so it runs in the host tests too. */
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    /* calibration */
    int calibrated;
    int cal_n;
    uint32_t cal_start_ms;
    float cal_sum[3], cal_min[3], cal_max[3];
    float bias[3];          /* deg/s */
    /* every sample */
    float gyro[3];          /* deg/s, bias removed */
    float accel[3];         /* g */
    float up[3];            /* unit vector, sensor frame */
    float gyro_mag;         /* |gyro| deg/s */
} motion_t;

void motion_init(motion_t *m, uint32_t now_ms);
/* Feed one raw sample (ax ay az gx gy gz). Returns 1 on the sample that
 * finishes calibration. Before that, gyro[] has no bias removed. */
int motion_update(motion_t *m, const int16_t raw[6], uint32_t now_ms);

float vdot(const float a[3], const float b[3]);
void vcross(const float a[3], const float b[3], float out[3]);
int vnormalize(float v[3]);   /* 0 if the vector was ~zero (left unchanged) */

#ifdef __cplusplus
}
#endif
