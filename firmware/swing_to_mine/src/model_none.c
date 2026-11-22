/* SWING_MODEL=OFF: no model, the decoder only ever sees "nothing". */
#include "model_runner.h"

uint32_t model_init(void) { return 0; }
int model_available(void) { return 0; }
int model_run(const int16_t window[MODEL_WINDOW][6], float *prob)
{
    (void)window;
    *prob = 1.0f;
    return 0;
}
