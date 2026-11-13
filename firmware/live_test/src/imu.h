/* MPU-6050 over I2C1 (PB6 = SCL, PB9 = SDA), set up like firmware/capture:
 * 100 Hz, +-16 g, +-2000 deg/s, ~42 Hz low-pass. Wiring: docs/wiring.md. */
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Returns 0 if the chip acknowledged every setup write. *who gets WHO_AM_I
 * (0x68 on a genuine chip, clones answer other values), or 0xFF if no answer. */
int imu_init(uint8_t *who);
int imu_data_ready(void);           /* 1 when a new sample is waiting (clears the flag) */
int imu_read(int16_t out[6]);       /* ax ay az gx gy gz raw counts; 0 = ok */

#ifdef __cplusplus
}
#endif
