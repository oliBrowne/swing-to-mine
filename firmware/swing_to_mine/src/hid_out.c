#include "hid_out.h"

#include <string.h>

#include "board.h"
#include "stm32f4xx.h"
#include "tusb.h"

/* TinyUSB's STM32 driver reads the core clock from here (we set the clocks
 * by hand in board.c, not with ST's system file). */
uint32_t SystemCoreClock = CORE_HZ;
uint32_t tusb_time_millis_api(void) { return board_ms(); }

static uint8_t modifier, keys[6], buttons;
static int32_t dx, dy, wheel;          /* motion not sent yet */
static int keyboard_dirty, mouse_dirty;

void usb_hw_init(void)
{
    /* OTG FS on CN5: PA11 = D-, PA12 = D+ (AF10), PA9 = VBUS sense (input) */
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN | RCC_AHB1ENR_GPIOCEN;
    (void)RCC->AHB1ENR;
    GPIOA->MODER = (GPIOA->MODER & ~(GPIO_MODER_MODER9 | GPIO_MODER_MODER11 | GPIO_MODER_MODER12)) |
                   (2u << GPIO_MODER_MODER11_Pos) | (2u << GPIO_MODER_MODER12_Pos);
    GPIOA->OSPEEDR |= (3u << GPIO_OSPEEDR_OSPEED11_Pos) | (3u << GPIO_OSPEEDR_OSPEED12_Pos);
    GPIOA->AFR[1] = (GPIOA->AFR[1] & ~((0xFu << 12) | (0xFu << 16))) | (10u << 12) | (10u << 16);

    /* PC0 switches 5 V onto the port for host mode (active low). We are a device: keep it off. */
    GPIOC->BSRR = 1u;
    GPIOC->MODER = (GPIOC->MODER & ~GPIO_MODER_MODER0) | (1u << GPIO_MODER_MODER0_Pos);

    RCC->AHB2ENR |= RCC_AHB2ENR_OTGFSEN;
    (void)RCC->AHB2ENR;

    tud_configure_dwc2_t cfg = CFG_TUD_CONFIGURE_DWC2_DEFAULT;
    cfg.vbus_sensing = true;
    tud_configure(0, TUD_CFGID_DWC2, &cfg);
    tusb_rhport_init_t init = {.role = TUSB_ROLE_DEVICE, .speed = TUSB_SPEED_FULL};
    tusb_init(0, &init);
}

void OTG_FS_IRQHandler(void) { tud_int_handler(0); }

int hid_out_mounted(void) { return tud_mounted(); }

void hid_out_set(const hid_frame_t *f)
{
    if (f->modifier != modifier || memcmp(f->keys, keys, sizeof(keys)) != 0) {
        modifier = f->modifier;
        memcpy(keys, f->keys, sizeof(keys));
        keyboard_dirty = 1;
    }
    if (f->buttons != buttons) {
        buttons = f->buttons;
        mouse_dirty = 1;
    }
    if (f->dx || f->dy || f->wheel) {
        dx += f->dx;
        dy += f->dy;
        wheel += f->wheel;
        mouse_dirty = 1;
    }
    /* not plugged in / not polled: don't pile up motion to dump later */
    if (!tud_mounted()) {
        dx = dy = wheel = 0;
    }
}

static int8_t take(int32_t *v)
{
    int32_t out = *v > 127 ? 127 : (*v < -127 ? -127 : *v);
    *v -= out;
    return (int8_t)out;
}

void hid_out_task(void)
{
    if (!keyboard_dirty && !mouse_dirty) return;
    if (tud_suspended()) {               /* PC asleep: wake it only if something is pressed */
        if (modifier || keys[0] || buttons) tud_remote_wakeup();
        return;
    }
    if (!tud_hid_ready()) return;

    /* one report per USB poll (1 ms); when both changed, take turns */
    static int last_was_mouse;
    if (mouse_dirty && (!keyboard_dirty || !last_was_mouse)) {
        int8_t x = take(&dx), y = take(&dy), w = take(&wheel);
        if (tud_hid_mouse_report(REPORT_ID_MOUSE, buttons, x, y, w, 0)) {
            mouse_dirty = dx || dy || wheel;
            last_was_mouse = 1;
        } else {
            dx += x; dy += y; wheel += w;   /* not sent: put it back */
        }
    } else if (keyboard_dirty) {
        if (tud_hid_keyboard_report(REPORT_ID_KEYBOARD, modifier, keys)) {
            keyboard_dirty = 0;
            last_was_mouse = 0;
        }
    }
}

/* No GET_REPORT / SET_REPORT (e.g. caps lock LED) needed. */
uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type,
                               uint8_t *buffer, uint16_t reqlen)
{
    (void)instance; (void)report_id; (void)report_type; (void)buffer; (void)reqlen;
    return 0;
}

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type,
                           uint8_t const *buffer, uint16_t bufsize)
{
    (void)instance; (void)report_id; (void)report_type; (void)buffer; (void)bufsize;
}
