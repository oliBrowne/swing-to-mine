#include "hid_out.h"
#include <string.h>
#include "tusb.h"

static uint8_t modifier;
static uint8_t keys[6];
static uint8_t buttons;
static int16_t dx, dy, wheel;  // pending motion, clamped to int8 when sent
static bool keyboard_dirty;
static bool mouse_dirty;

static int8_t take_int8(int16_t *v) {
  int16_t out = *v > 127 ? 127 : (*v < -127 ? -127 : *v);
  *v -= out;
  return (int8_t)out;
}

void hid_send(const hid_state_t *s) {
  if (s->modifier != modifier || memcmp(s->keys, keys, sizeof keys) != 0) {
    modifier = s->modifier;
    memcpy(keys, s->keys, sizeof keys);
    keyboard_dirty = true;
  }
  if (s->buttons != buttons || s->dx || s->dy || s->wheel) {
    buttons = s->buttons;
    dx += s->dx;
    dy += s->dy;
    wheel += s->wheel;
    mouse_dirty = true;
  }
}

void hid_release_all(void) {
  hid_state_t none = {0};
  dx = dy = wheel = 0;
  hid_send(&none);
}

void hid_out_task(void) {
  if (!keyboard_dirty && !mouse_dirty) return;

  // Wake the PC if it put the bus to sleep and we have input for it
  if (tud_suspended()) {
    tud_remote_wakeup();
    return;
  }
  if (!tud_hid_ready()) return;

  // One report per interrupt transfer; the other goes on a later call
  if (mouse_dirty) {
    int8_t x = take_int8(&dx), y = take_int8(&dy), w = take_int8(&wheel);
    if (tud_hid_mouse_report(REPORT_ID_MOUSE, buttons, x, y, w, 0)) {
      mouse_dirty = dx || dy || wheel;
    }
  } else if (tud_hid_keyboard_report(REPORT_ID_KEYBOARD, modifier, keys)) {
    keyboard_dirty = false;
  }
}

// Required TinyUSB callbacks: no GET_REPORT / SET_REPORT support needed yet
uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type,
                               uint8_t *buffer, uint16_t reqlen) {
  (void)instance; (void)report_id; (void)report_type; (void)buffer; (void)reqlen;
  return 0;
}

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type,
                           uint8_t const *buffer, uint16_t bufsize) {
  (void)instance; (void)report_id; (void)report_type; (void)buffer; (void)bufsize;
}
