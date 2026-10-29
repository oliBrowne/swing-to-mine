// TinyUSB configuration: device only, one HID interface (keyboard + mouse
// with report IDs) on the OTG FS port (CN5). CDC serial comes later (M2/M3).
#ifndef TUSB_CONFIG_H
#define TUSB_CONFIG_H

#define CFG_TUSB_OS            OPT_OS_NONE
#define CFG_TUSB_DEBUG         0
#define CFG_TUSB_MEM_ALIGN     __attribute__((aligned(4)))

#define CFG_TUD_ENABLED        1
#define CFG_TUD_MAX_SPEED      OPT_MODE_FULL_SPEED
#define CFG_TUD_ENDPOINT0_SIZE 64

#define CFG_TUD_HID            1
#define CFG_TUD_CDC            0
#define CFG_TUD_MSC            0
#define CFG_TUD_MIDI           0
#define CFG_TUD_VENDOR         0

// Largest report + 1 byte report ID (keyboard: 8 + 1)
#define CFG_TUD_HID_EP_BUFSIZE 16

#endif
