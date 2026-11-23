/* Swing to Mine controller firmware.
 *
 * The board shows up on the PC as a USB keyboard + mouse (micro-USB port CN5)
 * and plays Minecraft Java Edition: air mouse to look, tilt to walk, gestures
 * for clicks and the hotbar, the blue button for modes (docs/controls.md).
 *
 *   every 1 ms     button, taps, LEDs, USB reports
 *   every sample   (100 Hz from the MPU-6050) motion -> air mouse / walking
 *   every 50 ms    model on the last 1 s -> decoder -> clicks / scroll (look mode)
 *
 * NOT TESTED ON HARDWARE YET (Nov 22): built, and the logic is checked on a PC
 * (test/). Read the g_* variables over the ST-LINK to see what it's doing.
 */
#include <string.h>

#include "board.h"
#include "controller.h"
#include "gesture_decoder.h"
#include "hid_out.h"
#include "imu.h"
#include "model_runner.h"
#include "tusb.h"

#define REG(addr) (*(volatile uint32_t *)(addr))
#define RCC_AHB1ENR REG(0x40023830u)
#define GPIOA_IDR   REG(0x40020010u)

/* ---- status for the debugger ---- */
volatile uint32_t g_magic = 0;              /* 0x5717E000 once running */
volatile uint32_t g_imu_ok = 0;
volatile uint32_t g_who_am_i = 0xFF;
volatile uint32_t g_imu_errors = 0;
volatile uint32_t g_samples = 0;
volatile uint32_t g_model_error = 0;        /* from model_init(), 0 = ok */
volatile uint32_t g_model_errors = 0;       /* failed runs */
volatile uint32_t g_inferences = 0;
volatile int32_t g_last_class = -1;
volatile float g_last_prob = 0.0f;
volatile uint32_t g_events[8];              /* decoder events, by gesture_event_t */
volatile uint32_t g_mode = 0;               /* ctrl_mode_t */
volatile uint32_t g_paused = 0;
volatile uint32_t g_usb_mounted = 0;
volatile float g_fwd_deg = 0.0f, g_side_deg = 0.0f;   /* move-mode tilt */
volatile uint32_t g_loop_us_max = 0;        /* worst sample step incl. the model */
/* what went out to USB, counted at the hand-over to the USB code
 * (the simulator check reads these, since it has no USB port) */
volatile uint32_t g_out_left = 0, g_out_right = 0;   /* button presses (rising edges) */
volatile uint32_t g_out_left_ms = 0;                 /* ms the left button was held (mining) */
volatile int32_t g_out_wheel = 0;                    /* sum of scroll steps, + = up/left */
volatile uint32_t g_out_wheel_steps = 0;             /* scroll steps either way */
volatile uint32_t g_out_keys = 0;                    /* key/modifier presses (rising edges) */

static controller_t ctrl;
static decoder_t dec;
static int16_t ring[MODEL_WINDOW][6];
static int16_t window[MODEL_WINDOW][6];

#define DEMCR      REG(0xE000EDFCu)
#define DWT_CTRL   REG(0xE0001000u)
#define DWT_CYCCNT REG(0xE0001004u)
#define DWT_LAR    REG(0xE0001FB0u)

static int button_pressed(void) { return (GPIOA_IDR & 1u) != 0; }   /* PA0, pulled down on the board */

static void on_sample(const int16_t s[6], uint32_t now)
{
    static uint32_t head = 0, filled = 0, since_run = 0;

    controller_sample(&ctrl, s, now);
    memcpy(ring[head], s, sizeof(ring[0]));
    head = (head + 1) % MODEL_WINDOW;
    if (filled < MODEL_WINDOW) filled++;
    g_samples = g_samples + 1;

    if (filled < MODEL_WINDOW || ++since_run < MODEL_STRIDE || !model_available()) return;
    since_run = 0;
    for (int t = 0; t < MODEL_WINDOW; t++) memcpy(window[t], ring[(head + t) % MODEL_WINDOW], sizeof(window[0]));
    float prob;
    int top = model_run(window, &prob);
    g_last_class = top;
    g_last_prob = prob;
    g_inferences = g_inferences + 1;
    gesture_event_t ev = decoder_update(&dec, top, prob, now);
    if (ev != GEV_NONE) {
        g_events[ev] = g_events[ev] + 1;
        controller_gesture(&ctrl, ev, now);
    }
}

static void count_output(const hid_frame_t *f)
{
    static hid_frame_t last;
    if ((f->buttons & MOUSE_LEFT) && !(last.buttons & MOUSE_LEFT)) g_out_left = g_out_left + 1;
    if ((f->buttons & MOUSE_RIGHT) && !(last.buttons & MOUSE_RIGHT)) g_out_right = g_out_right + 1;
    if (f->buttons & MOUSE_LEFT) g_out_left_ms = g_out_left_ms + 1;
    if (f->wheel) {
        g_out_wheel = g_out_wheel + f->wheel;
        g_out_wheel_steps = g_out_wheel_steps + (uint32_t)(f->wheel > 0 ? f->wheel : -f->wheel);
    }
    if ((f->modifier & ~last.modifier) || (f->keys[0] && f->keys[0] != last.keys[0])) g_out_keys = g_out_keys + 1;
    last = *f;
}

int main(void)
{
    board_init();
    RCC_AHB1ENR |= 1u;                 /* GPIOA clock for the button (PA0 is an input after reset) */
    DEMCR |= (1u << 24);
    DWT_LAR = 0xC5ACCE55u;
    DWT_CTRL |= 1u;

    usb_hw_init();
    g_model_error = model_init();
    decoder_init(&dec);
    controller_init(&ctrl, board_ms());
    g_magic = 0x5717E000u;

    uint32_t last_ms = board_ms(), last_sample_ms = last_ms, last_try_ms = 0;
    int16_t s[6];
    while (1) {
        tud_task();
        uint32_t now = board_ms();

        /* sensor: (re)connect once a second until it answers */
        if (!g_imu_ok && (last_try_ms == 0 || now - last_try_ms >= 1000)) {
            uint8_t who = 0xFF;
            last_try_ms = now ? now : 1;
            g_imu_ok = imu_init(&who) == 0;
            g_who_am_i = who;
            if (!g_imu_ok) g_imu_errors = g_imu_errors + 1;
            else controller_init(&ctrl, board_ms());   /* calibrate again from the start */
            now = board_ms();
        }
        /* new sample? (only ask the sensor near the 10 ms mark; fall back to timing on clones) */
        if (g_imu_ok && now - last_sample_ms >= 9 && (imu_data_ready() || now - last_sample_ms >= 11)) {
            uint32_t c0 = DWT_CYCCNT;
            static uint32_t errors_in_a_row = 0;
            if (imu_read(s) == 0) {
                errors_in_a_row = 0;
                last_sample_ms = now;
                on_sample(s, now);
            } else {
                g_imu_errors = g_imu_errors + 1;
                if (++errors_in_a_row >= 20) {         /* ~0.2 s of errors: wire came loose? start over */
                    errors_in_a_row = 0;
                    g_imu_ok = 0;
                    hid_frame_t none;
                    memset(&none, 0, sizeof(none));
                    hid_out_set(&none);                /* let go of everything */
                }
            }
            uint32_t us = (DWT_CYCCNT - c0) / (CORE_HZ / 1000000u);
            if (us > g_loop_us_max) g_loop_us_max = us;
        }

        if (now != last_ms) {          /* 1 kHz */
            last_ms = now;
            controller_tick(&ctrl, button_pressed(), now);
            hid_frame_t f;
            if (g_imu_ok) controller_frame(&ctrl, now, &f);
            else memset(&f, 0, sizeof(f));             /* no sensor: nothing held */
            hid_out_set(&f);
            count_output(&f);
            if (g_imu_ok) led_set(controller_leds(&ctrl, now));
            else led_set((now / 100) % 2 ? LED_RED : 0);   /* red fast blink: sensor not answering */
            g_mode = ctrl.mode;
            g_paused = (uint32_t)ctrl.paused;
            g_usb_mounted = (uint32_t)hid_out_mounted();
            g_fwd_deg = ctrl.walk.fwd_deg;
            g_side_deg = ctrl.walk.side_deg;
        }
        hid_out_task();
    }
}
