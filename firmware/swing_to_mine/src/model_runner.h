/* The gesture model behind one small interface, so the firmware also builds
 * without ExecuTorch (SWING_MODEL=OFF: buttons, air mouse and walking work,
 * gestures never fire). */
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MODEL_WINDOW 100   /* samples = 1 s at 100 Hz */
#define MODEL_STRIDE 5     /* run every 5 samples = 50 ms */

/* 0 = ok. Otherwise stage * 256 + ExecuTorch error code. */
uint32_t model_init(void);
/* window = last 1 s of raw counts, oldest first. Returns the top class
 * (training/labels.py order) and its softmax probability in *prob. */
int model_run(const int16_t window[MODEL_WINDOW][6], float *prob);
int model_available(void);

#ifdef __cplusplus
}
#endif
