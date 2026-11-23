/* Blue button timing (docs/controls.md "The button"). Feed it the raw pin
 * level every millisecond; it debounces and reports what kind of press it was.
 * Pure logic: the pin is read in main.c. */
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BTN_NONE = 0,
    BTN_SINGLE,   /* play: one short press (sent ~300 ms after release, once no 2nd press came) */
    BTN_DOUBLE,   /* play: two short presses */
    BTN_CLICK,    /* inventory: a short press, sent as soon as it is released */
    BTN_HOLD,     /* released after 1 s (before 3 s) */
    BTN_RESYNC,   /* released after 3 s */
} button_event_t;

typedef struct {
    int raw, stable;
    uint32_t raw_since;
    int state;              /* internal */
    uint32_t down_ms, up_ms;
    uint32_t held_ms;       /* how long it has been held now (0 = up), for LED feedback */
} button_t;

void button_init(button_t *b);
/* instant_click = 1 in the inventory (no double press there: click on release) */
button_event_t button_update(button_t *b, int pressed, int instant_click, uint32_t now_ms);

#ifdef __cplusplus
}
#endif
