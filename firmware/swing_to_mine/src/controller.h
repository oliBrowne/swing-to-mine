/* The controller: modes, button, LEDs and what goes out over USB.
 * Follows docs/controls.md. Everything here is plain logic (no hardware), so
 * test/ runs it on a PC. main.c feeds it:
 *   controller_tick()    every 1 ms with the button pin,
 *   controller_sample()  every sensor sample (100 Hz),
 *   controller_gesture() whenever the gesture decoder fires,
 * and reads back controller_frame() (keys/buttons/mouse) and controller_leds().
 *
 * Modes: CALIBRATE (power-on, keep still) -> LOOK (green) <-> MOVE (orange),
 * INVENTORY (blue). Single press LOOK<->MOVE, hold 1 s = inventory (taps E),
 * double press = crouch (taps Shift), hold 3 s = resync (no key). */
#pragma once
#include <stdint.h>

#include "air_mouse.h"
#include "button.h"
#include "gesture_decoder.h"
#include "motion.h"
#include "tilt_walk.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { MODE_CALIBRATE, MODE_LOOK, MODE_MOVE, MODE_INVENTORY } ctrl_mode_t;

/* USB HID usage codes (HID Usage Tables, keyboard page) */
#define KEY_A 0x04
#define KEY_D 0x07
#define KEY_E 0x08
#define KEY_S 0x16
#define KEY_W 0x1A
#define KEY_SPACE 0x2C
#define MOD_LCTRL 0x01
#define MOD_LSHIFT 0x02
#define MOUSE_LEFT 0x01
#define MOUSE_RIGHT 0x02

/* LED bits, as board.c's led_set(): green PD12, orange PD13, red PD14, blue PD15 */
#define LED_GREEN 0x1
#define LED_ORANGE 0x2
#define LED_RED 0x4
#define LED_BLUE 0x8

typedef struct {
    uint8_t modifier;   /* MOD_* bits held */
    uint8_t keys[6];    /* KEY_* codes held, 0 = empty */
    uint8_t buttons;    /* MOUSE_* bits held */
    int16_t dx, dy;     /* mouse motion since the last frame */
    int8_t wheel;       /* + = scroll up = hotbar slot to the left */
} hid_frame_t;

enum { TAP_E, TAP_SHIFT, TAP_SPACE, TAP_LEFT, TAP_RIGHT, TAP_COUNT };

typedef struct {
    ctrl_mode_t mode, play_mode;
    motion_t motion;
    air_mouse_t air;
    tilt_walk_t walk;
    button_t button;
    int paused;              /* rest pose: no output */
    uint32_t rest_since;     /* 0 = not resting */
    int mining;              /* left button held for mine */
    uint32_t tap_until[TAP_COUNT];
    int tap_on[TAP_COUNT];
    int16_t dx, dy;          /* waiting for the next frame */
    int8_t wheel;
    /* LED effects */
    uint32_t red_until;
    uint8_t blink_mask;
    int blink_count;
    uint32_t blink_start;
    /* counters for debugging (read with the debugger / tests) */
    uint32_t n_gestures_used, n_gestures_ignored, n_jumps;
} controller_t;

void controller_init(controller_t *c, uint32_t now_ms);
void controller_tick(controller_t *c, int button_pressed, uint32_t now_ms);
void controller_sample(controller_t *c, const int16_t raw[6], uint32_t now_ms);
void controller_gesture(controller_t *c, gesture_event_t ev, uint32_t now_ms);
/* What to send now: held keys/buttons, plus motion collected since the last call. */
void controller_frame(controller_t *c, uint32_t now_ms, hid_frame_t *out);
uint8_t controller_leds(const controller_t *c, uint32_t now_ms);

#ifdef __cplusplus
}
#endif
