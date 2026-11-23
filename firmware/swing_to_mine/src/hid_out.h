/* USB HID output: one interface, report ID 1 = keyboard, 2 = mouse.
 * hid_out_set() takes what the controller wants held right now plus new mouse
 * motion; hid_out_task() sends whatever changed when USB is ready. */
#pragma once
#include <stdint.h>

#include "controller.h"

#ifdef __cplusplus
extern "C" {
#endif

enum { REPORT_ID_KEYBOARD = 1, REPORT_ID_MOUSE = 2 };

void usb_hw_init(void);              /* clocks + pins for OTG FS, then TinyUSB */
void hid_out_set(const hid_frame_t *f);
void hid_out_task(void);             /* call often from the main loop, after tud_task() */
int hid_out_mounted(void);

#ifdef __cplusplus
}
#endif
