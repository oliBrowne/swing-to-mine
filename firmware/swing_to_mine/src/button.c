#include "button.h"

#include <string.h>

#include "config.h"

enum { ST_IDLE, ST_DOWN, ST_WAIT_SECOND, ST_SECOND_DOWN };

void button_init(button_t *b)
{
    memset(b, 0, sizeof(*b));
}

/* what a press of this length means when it is let go */
static button_event_t long_press(uint32_t dur)
{
    if (dur >= BTN_RESYNC_MS) return BTN_RESYNC;
    if (dur >= BTN_HOLD_MS) return BTN_HOLD;
    return BTN_NONE;                         /* 400 ms - 1 s: backed out, nothing */
}

button_event_t button_update(button_t *b, int pressed, int instant_click, uint32_t now)
{
    /* debounce */
    pressed = pressed ? 1 : 0;
    if (pressed != b->raw) {
        b->raw = pressed;
        b->raw_since = now;
    }
    int edge = 0;
    if (b->raw != b->stable && now - b->raw_since >= BTN_DEBOUNCE_MS) {
        b->stable = b->raw;
        edge = b->stable ? 1 : -1;
    }
    b->held_ms = b->stable ? now - b->down_ms : 0;

    switch (b->state) {
    case ST_IDLE:
        if (edge > 0) {
            b->down_ms = now;
            b->held_ms = 0;
            b->state = ST_DOWN;
        }
        break;
    case ST_DOWN:
        if (edge < 0) {
            uint32_t dur = now - b->down_ms;
            b->state = ST_IDLE;
            if (dur >= BTN_SHORT_MS) return long_press(dur);
            if (instant_click) return BTN_CLICK;
            b->up_ms = now;
            b->state = ST_WAIT_SECOND;
        }
        break;
    case ST_WAIT_SECOND:
        if (edge > 0) {
            b->down_ms = now;
            b->held_ms = 0;
            b->state = ST_SECOND_DOWN;
        } else if (now - b->up_ms > BTN_DOUBLE_GAP_MS) {
            b->state = ST_IDLE;
            return BTN_SINGLE;
        }
        break;
    case ST_SECOND_DOWN:
        if (edge < 0) {
            uint32_t dur = now - b->down_ms;
            b->state = ST_IDLE;
            /* a second press that turned into a hold counts as the hold */
            return dur >= BTN_SHORT_MS ? long_press(dur) : BTN_DOUBLE;
        }
        break;
    }
    return BTN_NONE;
}
