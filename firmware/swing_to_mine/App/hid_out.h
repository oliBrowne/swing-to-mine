// HID output: one USB HID interface with two report IDs (keyboard, mouse).
// Callers describe the state they want; hid_out_task() sends whatever changed.
#ifndef HID_OUT_H
#define HID_OUT_H

#include <stdint.h>

enum {
  REPORT_ID_KEYBOARD = 1,
  REPORT_ID_MOUSE,
};

// Mouse button bits (same as the HID boot mouse layout)
#define HID_BTN_LEFT   0x01
#define HID_BTN_RIGHT  0x02
#define HID_BTN_MIDDLE 0x04

typedef struct {
  uint8_t modifier;  // KEYBOARD_MODIFIER_* bits
  uint8_t keys[6];   // HID_KEY_* usage codes, 0 = none
  uint8_t buttons;   // HID_BTN_* bits, held until changed
  int8_t dx, dy;     // relative motion, added to anything not yet sent
  int8_t wheel;
} hid_state_t;

void hid_send(const hid_state_t *state);
void hid_release_all(void);

// Call from the main loop after tud_task().
void hid_out_task(void);

#endif
