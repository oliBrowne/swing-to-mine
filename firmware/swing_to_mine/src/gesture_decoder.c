#include "gesture_decoder.h"

void decoder_init(decoder_t *d)
{
    d->streak_class = -1;
    d->streak = 0;
    d->mining = 0;
    d->cooldown_until = 0;
    d->mine_last = 0;
    d->blocked = -1;
    d->lost = 0;
}

gesture_event_t decoder_update(decoder_t *d, int raw, float prob, uint32_t now)
{
    int top = (prob >= DEC_MIN_PROB) ? raw : CLS_NOTHING;
    d->streak = (top == d->streak_class) ? d->streak + 1 : 1;
    d->streak_class = top;

    if (d->blocked >= 0) {
        d->lost = (raw != d->blocked) ? d->lost + 1 : 0;
        if (d->lost >= DEC_REARM_RUNS) d->blocked = -1;
    }
    if (d->mining) {
        if (raw == CLS_MINE) {
            d->mine_last = now;
        } else if (now - d->mine_last > DEC_MINE_RELEASE_MS) {
            d->mining = 0;
            d->cooldown_until = now + DEC_COOLDOWN_MS;
            return GEV_MINE_STOP;
        }
        return GEV_NONE;
    }
    if (top == CLS_NOTHING || d->streak < DEC_AGREE_RUNS ||
        (int32_t)(now - d->cooldown_until) < 0 || top == d->blocked)
        return GEV_NONE;

    if (top == CLS_MINE) {
        d->mining = 1;
        d->mine_last = now;
        return GEV_MINE_START;
    }
    d->cooldown_until = now + DEC_COOLDOWN_MS;
    d->blocked = top;
    d->lost = 0;
    switch (top) {
    case CLS_SWORD:      return GEV_SWORD;
    case CLS_PLACE:      return GEV_PLACE;
    case CLS_SWAP_LEFT:  return GEV_SWAP_LEFT;
    default:             return GEV_SWAP_RIGHT;
    }
}
