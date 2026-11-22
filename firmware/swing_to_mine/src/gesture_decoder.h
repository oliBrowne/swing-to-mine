/* Gesture decoder: the model's winner every 50 ms -> game events.
 *
 * Same rules as decode() in training/evaluate_stream.py and
 * firmware/live_test/src/main.cpp (keep all three in step):
 *  - a gesture fires after DEC_AGREE_RUNS confident wins in a row, outside the cooldown;
 *  - after firing it is blocked until the model has named something else
 *    DEC_REARM_RUNS runs in a row (one swing stays in the 1 s window a while);
 *  - mine is a hold: it starts like the others and stops once mine has not
 *    been the top pick for DEC_MINE_RELEASE_MS.
 * test/test_decoder_parity.py checks this file against the Python version. */
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* classes in training/labels.py order (test/test_decoder_parity.py checks it) */
enum { CLS_NOTHING, CLS_SWORD, CLS_MINE, CLS_PLACE, CLS_SWAP_LEFT, CLS_SWAP_RIGHT, N_CLASSES };

#define DEC_MIN_PROB        0.60f
#define DEC_AGREE_RUNS      3
#define DEC_COOLDOWN_MS     300
#define DEC_REARM_RUNS      3
#define DEC_MINE_RELEASE_MS 400

typedef enum {
    GEV_NONE = 0,
    GEV_SWORD,       /* one left click */
    GEV_PLACE,       /* one right click */
    GEV_SWAP_LEFT,   /* scroll one slot left */
    GEV_SWAP_RIGHT,  /* scroll one slot right */
    GEV_MINE_START,  /* hold left click ... */
    GEV_MINE_STOP,   /* ... until this */
} gesture_event_t;

typedef struct {
    int streak_class, streak;
    int mining;
    uint32_t cooldown_until, mine_last;
    int blocked, lost;
} decoder_t;

void decoder_init(decoder_t *d);
/* top = model's top class, prob = its softmax probability, now = ms */
gesture_event_t decoder_update(decoder_t *d, int top, float prob, uint32_t now);

#ifdef __cplusplus
}
#endif
