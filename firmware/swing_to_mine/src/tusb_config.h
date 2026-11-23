/* TinyUSB settings: device only, full speed, on the Discovery's micro-USB
 * port CN5 (USB OTG FS). One HID interface carries both the keyboard and the
 * mouse report (report IDs 1 and 2). */
#pragma once

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

/* largest report (keyboard: 8 bytes) + 1 byte report ID */
#define CFG_TUD_HID_EP_BUFSIZE 16
