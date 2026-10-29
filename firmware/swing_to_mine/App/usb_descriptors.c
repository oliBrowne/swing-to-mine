// USB descriptors: one HID interface carrying keyboard + mouse reports.
#include <string.h>
#include "stm32f4xx.h"
#include "tusb.h"
#include "hid_out.h"

// 0xCAFE is TinyUSB's test vendor ID: fine for a class project, not for sale.
#define USB_VID 0xCAFE
#define USB_PID 0x5701

static const tusb_desc_device_t desc_device = {
  .bLength            = sizeof(tusb_desc_device_t),
  .bDescriptorType    = TUSB_DESC_DEVICE,
  .bcdUSB             = 0x0200,
  .bDeviceClass       = 0x00,
  .bDeviceSubClass    = 0x00,
  .bDeviceProtocol    = 0x00,
  .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,
  .idVendor           = USB_VID,
  .idProduct          = USB_PID,
  .bcdDevice          = 0x0100,
  .iManufacturer      = 0x01,
  .iProduct           = 0x02,
  .iSerialNumber      = 0x03,
  .bNumConfigurations = 0x01,
};

uint8_t const *tud_descriptor_device_cb(void) {
  return (uint8_t const *)&desc_device;
}

static const uint8_t desc_hid_report[] = {
  TUD_HID_REPORT_DESC_KEYBOARD(HID_REPORT_ID(REPORT_ID_KEYBOARD)),
  TUD_HID_REPORT_DESC_MOUSE(HID_REPORT_ID(REPORT_ID_MOUSE)),
};

uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance) {
  (void)instance;
  return desc_hid_report;
}

enum { ITF_NUM_HID, ITF_NUM_TOTAL };

#define EPNUM_HID        0x81
#define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_HID_DESC_LEN)

static const uint8_t desc_configuration[] = {
  TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),
  // 1 ms polling interval (see the timing budget in docs/architecture.md)
  TUD_HID_DESCRIPTOR(ITF_NUM_HID, 0, HID_ITF_PROTOCOL_NONE, sizeof(desc_hid_report), EPNUM_HID,
                     CFG_TUD_HID_EP_BUFSIZE, 1),
};

uint8_t const *tud_descriptor_configuration_cb(uint8_t index) {
  (void)index;
  return desc_configuration;
}

static const char *const string_desc[] = {
  NULL,                 // 0: language (handled below)
  "Swing to Mine",      // 1: manufacturer
  "Swing to Mine controller",  // 2: product
  NULL,                 // 3: serial, from the chip's unique ID
};

static uint16_t desc_str[32 + 1];

uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
  (void)langid;
  size_t n = 0;

  if (index == 0) {
    desc_str[1] = 0x0409;  // English (US)
    n = 1;
  } else if (index == 3) {
    const uint32_t *uid = (const uint32_t *)UID_BASE;
    for (int w = 0; w < 3; w++) {
      for (int nib = 7; nib >= 0; nib--) {
        desc_str[1 + n++] = "0123456789ABCDEF"[(uid[w] >> (4 * nib)) & 0xF];
      }
    }
  } else if (index < TU_ARRAY_SIZE(string_desc)) {
    const char *s = string_desc[index];
    n = strlen(s);
    if (n > 32) n = 32;
    for (size_t i = 0; i < n; i++) desc_str[1 + i] = (uint8_t)s[i];
  } else {
    return NULL;
  }

  desc_str[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * n + 2));
  return desc_str;
}
