/* Host tests for the controller logic. Synthetic sensor samples go in, the
 * USB frames and LEDs that would go to the PC come out.
 *   make -C firmware/swing_to_mine/test */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "button.h"
#include "config.h"
#include "controller.h"

static int failures = 0, checks = 0;
#define CHECK(cond, ...)                                                   \
    do {                                                                   \
        checks++;                                                          \
        if (!(cond)) {                                                     \
            failures++;                                                    \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                    \
            printf(__VA_ARGS__);                                           \
            printf("\n");                                                  \
        }                                                                  \
    } while (0)

/* ---------- a tiny simulated board ---------- */
typedef struct {
    controller_t c;
    uint32_t now;
    int pressed;
    float up[3];          /* board's "up" in the sensor frame (unit) */
    float gyro_dps[3];    /* what the gyro reads (before the bias) */
    float extra_g[3];     /* extra acceleration, in g */
    float bias_dps[3];
    /* what the PC saw */
    long sum_dx, sum_dy, sum_wheel;
    int key_e_presses, shift_presses, left_presses, right_presses, space_presses;
    int left_held_ms, ctrl_held_ms, w_held_ms, a_held_ms, s_held_ms, d_held_ms;
    hid_frame_t last;
} sim_t;

static int has_key(const hid_frame_t *f, uint8_t k)
{
    for (int i = 0; i < 6; i++)
        if (f->keys[i] == k) return 1;
    return 0;
}

static void sim_init(sim_t *s)
{
    memset(s, 0, sizeof(*s));
    s->up[UPRIGHT_AXIS] = -1.0f;   /* held upright, like in our recordings */
    s->bias_dps[0] = 1.2f; s->bias_dps[1] = -0.7f; s->bias_dps[2] = 0.4f;
    s->now = 1;
    controller_init(&s->c, s->now);
}

/* rotate "up" the way the sensor sees it when the board turns at gyro_dps */
static void sim_rotate_up(sim_t *s)
{
    float w[3], d[3];
    for (int i = 0; i < 3; i++) w[i] = s->gyro_dps[i] * 3.14159265f / 180.0f * 0.001f;
    vcross(w, s->up, d);
    for (int i = 0; i < 3; i++) s->up[i] -= d[i];
    vnormalize(s->up);
}

static void sim_ms(sim_t *s, int ms)
{
    for (int k = 0; k < ms; k++) {
        s->now++;
        sim_rotate_up(s);
        if (s->now % 10 == 0) {
            int16_t raw[6];
            for (int i = 0; i < 3; i++) {
                raw[i] = (int16_t)lroundf((s->up[i] + s->extra_g[i]) * ACCEL_COUNTS_PER_G);
                raw[3 + i] = (int16_t)lroundf((s->gyro_dps[i] + s->bias_dps[i]) * GYRO_COUNTS_PER_DPS);
            }
            controller_sample(&s->c, raw, s->now);
        }
        controller_tick(&s->c, s->pressed, s->now);
        hid_frame_t f;
        controller_frame(&s->c, s->now, &f);
        s->sum_dx += f.dx;
        s->sum_dy += f.dy;
        s->sum_wheel += f.wheel;
        if (has_key(&f, KEY_E) && !has_key(&s->last, KEY_E)) s->key_e_presses++;
        if (has_key(&f, KEY_SPACE) && !has_key(&s->last, KEY_SPACE)) s->space_presses++;
        if ((f.modifier & MOD_LSHIFT) && !(s->last.modifier & MOD_LSHIFT)) s->shift_presses++;
        if ((f.buttons & MOUSE_LEFT) && !(s->last.buttons & MOUSE_LEFT)) s->left_presses++;
        if ((f.buttons & MOUSE_RIGHT) && !(s->last.buttons & MOUSE_RIGHT)) s->right_presses++;
        if (f.buttons & MOUSE_LEFT) s->left_held_ms++;
        if (f.modifier & MOD_LCTRL) s->ctrl_held_ms++;
        if (has_key(&f, KEY_W)) s->w_held_ms++;
        if (has_key(&f, KEY_A)) s->a_held_ms++;
        if (has_key(&f, KEY_S)) s->s_held_ms++;
        if (has_key(&f, KEY_D)) s->d_held_ms++;
        s->last = f;
    }
}

static void sim_reset_counts(sim_t *s)
{
    hid_frame_t keep = s->last;
    controller_t c = s->c;
    float up[3], g[3], e[3], b[3];
    memcpy(up, s->up, sizeof(up)); memcpy(g, s->gyro_dps, sizeof(g));
    memcpy(e, s->extra_g, sizeof(e)); memcpy(b, s->bias_dps, sizeof(b));
    uint32_t now = s->now; int pressed = s->pressed;
    memset(s, 0, sizeof(*s));
    s->c = c; s->now = now; s->pressed = pressed; s->last = keep;
    memcpy(s->up, up, sizeof(up)); memcpy(s->gyro_dps, g, sizeof(g));
    memcpy(s->extra_g, e, sizeof(e)); memcpy(s->bias_dps, b, sizeof(b));
}

static void press(sim_t *s, int ms)
{
    s->pressed = 1;
    sim_ms(s, ms);
    s->pressed = 0;
}

/* power on, keep still, end up in look mode */
static void boot(sim_t *s)
{
    sim_init(s);
    sim_ms(s, 2000);
}

/* ---------- tests ---------- */
static void test_calibration(void)
{
    sim_t s;
    sim_init(&s);
    sim_ms(&s, 500);
    CHECK(s.c.mode == MODE_CALIBRATE, "still calibrating after 0.5 s");
    s.gyro_dps[2] = 20.0f;          /* someone moves it: start over */
    sim_ms(&s, 100);
    s.gyro_dps[2] = 0.0f;
    sim_ms(&s, 1200);
    CHECK(s.c.mode == MODE_CALIBRATE, "moving restarts the count (mode %d)", s.c.mode);
    sim_ms(&s, 600);
    CHECK(s.c.mode == MODE_LOOK, "look mode after 1.5 s still (mode %d)", s.c.mode);
    for (int i = 0; i < 3; i++)
        CHECK(fabsf(s.c.motion.bias[i] - s.bias_dps[i]) < 0.1f, "bias axis %d: %.2f vs %.2f", i,
              s.c.motion.bias[i], s.bias_dps[i]);
    CHECK(s.sum_dx == 0 && s.sum_dy == 0, "no mouse motion while calibrating");
    CHECK(controller_leds(&s.c, s.now) == LED_GREEN, "green in look mode");
    CHECK(!s.c.motion.cal_fallback, "a still board calibrates the exact way");
}

/* Held in a hand it never gets still enough: after ~3 s the calmest second is used. */
static void test_calibration_in_hand(void)
{
    sim_t s;
    sim_init(&s);
    for (int ms = 0; ms < 2900; ms++) {       /* hand shake: +-6 deg/s wobble on two axes */
        s.gyro_dps[0] = 6.0f * sinf(ms * 0.045f);
        s.gyro_dps[2] = 5.0f * sinf(ms * 0.031f + 1.0f);
        sim_ms(&s, 1);
    }
    CHECK(s.c.mode == MODE_CALIBRATE, "shaky hand: still calibrating before 3 s (mode %d)", s.c.mode);
    for (int ms = 2900; ms < 4200 && s.c.mode == MODE_CALIBRATE; ms++) {
        s.gyro_dps[0] = 6.0f * sinf(ms * 0.045f);
        s.gyro_dps[2] = 5.0f * sinf(ms * 0.031f + 1.0f);
        sim_ms(&s, 1);
    }
    CHECK(s.c.mode == MODE_LOOK, "shaky hand: look mode soon after 3 s (mode %d, t %u)", s.c.mode, (unsigned)s.now);
    CHECK(s.now < 4200, "shaky hand: done by ~4 s (t %u)", (unsigned)s.now);
    CHECK(s.c.motion.cal_fallback, "shaky hand: used the fallback");
    for (int i = 0; i < 3; i++)
        CHECK(fabsf(s.c.motion.bias[i] - s.bias_dps[i]) < POINT_DEADZONE_DPS, "shaky hand bias axis %d: %.2f vs %.2f",
              i, s.c.motion.bias[i], s.bias_dps[i]);
    /* then held still: the left-over error must not drift the camera */
    s.gyro_dps[0] = s.gyro_dps[2] = 0.0f;
    sim_reset_counts(&s);
    sim_ms(&s, 3000);
    CHECK(s.sum_dx == 0 && s.sum_dy == 0, "no drift after a hand-held calibration (%ld, %ld)", s.sum_dx, s.sum_dy);

    /* waved about: too wild for the fallback, it waits; one calmer second ends it */
    sim_init(&s);
    for (int ms = 0; ms < 6000; ms++) {
        s.gyro_dps[1] = 60.0f * sinf(ms * 0.02f);
        sim_ms(&s, 1);
    }
    CHECK(s.c.mode == MODE_CALIBRATE, "waving it about: still calibrating after 6 s (mode %d)", s.c.mode);
    for (int ms = 0; ms < 2100 && s.c.mode == MODE_CALIBRATE; ms++) {
        s.gyro_dps[1] = 4.0f * sinf(ms * 0.045f);
        sim_ms(&s, 1);
    }
    CHECK(s.c.mode == MODE_LOOK, "then a calm hand for 2 s: look mode (mode %d)", s.c.mode);
}

static void test_button_timing(void)
{
    button_t b;
    button_init(&b);
    uint32_t t = 0;
    button_event_t ev, got;
    /* single */
    got = BTN_NONE;
    for (int i = 0; i < 100; i++) if ((ev = button_update(&b, 1, 0, ++t))) got = ev;
    for (int i = 0; i < 400; i++) if ((ev = button_update(&b, 0, 0, ++t))) got = ev;
    CHECK(got == BTN_SINGLE, "single press -> %d", got);
    /* double */
    got = BTN_NONE;
    for (int i = 0; i < 80; i++) if ((ev = button_update(&b, 1, 0, ++t))) got = ev;
    for (int i = 0; i < 150; i++) if ((ev = button_update(&b, 0, 0, ++t))) got = ev;
    CHECK(got == BTN_NONE, "nothing yet between the two presses (%d)", got);
    for (int i = 0; i < 80; i++) if ((ev = button_update(&b, 1, 0, ++t))) got = ev;
    for (int i = 0; i < 400; i++) if ((ev = button_update(&b, 0, 0, ++t))) got = ev;
    CHECK(got == BTN_DOUBLE, "double press -> %d", got);
    /* bouncy contacts count as one press */
    got = BTN_NONE;
    int pattern[] = {1, 0, 1, 0, 1};
    for (int k = 0; k < 5; k++)
        for (int i = 0; i < 3; i++) if ((ev = button_update(&b, pattern[k], 0, ++t))) got = ev;
    for (int i = 0; i < 100; i++) if ((ev = button_update(&b, 1, 0, ++t))) got = ev;
    for (int i = 0; i < 400; i++) if ((ev = button_update(&b, 0, 0, ++t))) got = ev;
    CHECK(got == BTN_SINGLE, "bouncy press -> %d", got);
    /* 0.6 s: backed out, nothing */
    got = BTN_NONE;
    for (int i = 0; i < 600; i++) if ((ev = button_update(&b, 1, 0, ++t))) got = ev;
    for (int i = 0; i < 400; i++) if ((ev = button_update(&b, 0, 0, ++t))) got = ev;
    CHECK(got == BTN_NONE, "0.6 s press does nothing (%d)", got);
    /* hold 1.5 s, acts on release */
    got = BTN_NONE;
    for (int i = 0; i < 1500; i++) if ((ev = button_update(&b, 1, 0, ++t))) got = ev;
    CHECK(got == BTN_NONE, "hold does nothing until let go");
    CHECK(b.held_ms >= 1400, "held_ms counts up (%u)", (unsigned)b.held_ms);
    for (int i = 0; i < 50; i++) if ((ev = button_update(&b, 0, 0, ++t))) got = ev;
    CHECK(got == BTN_HOLD, "1.5 s hold -> %d", got);
    /* hold 3.5 s */
    got = BTN_NONE;
    for (int i = 0; i < 3500; i++) if ((ev = button_update(&b, 1, 0, ++t))) got = ev;
    for (int i = 0; i < 50; i++) if ((ev = button_update(&b, 0, 0, ++t))) got = ev;
    CHECK(got == BTN_RESYNC, "3.5 s hold -> %d", got);
    /* inventory: click right on release */
    got = BTN_NONE;
    for (int i = 0; i < 100; i++) if ((ev = button_update(&b, 1, 1, ++t))) got = ev;
    int when = -1;
    for (int i = 0; i < 400; i++) if ((ev = button_update(&b, 0, 1, ++t))) { got = ev; if (when < 0) when = i; }
    CHECK(got == BTN_CLICK && when <= BTN_DEBOUNCE_MS, "inventory click on release (%d after %d ms)", got, when);
    /* inventory: 1 s hold never clicks */
    got = BTN_NONE;
    for (int i = 0; i < 1200; i++) if ((ev = button_update(&b, 1, 1, ++t))) got = ev;
    for (int i = 0; i < 50; i++) if ((ev = button_update(&b, 0, 1, ++t))) got = ev;
    CHECK(got == BTN_HOLD, "inventory hold -> %d", got);
}

static void test_modes(void)
{
    sim_t s;
    boot(&s);
    CHECK(s.c.mode == MODE_LOOK, "boots into look");

    press(&s, 100); sim_ms(&s, 400);
    CHECK(s.c.mode == MODE_MOVE, "single press -> move (%d)", s.c.mode);
    CHECK(controller_leds(&s.c, s.now) == LED_ORANGE, "orange in move mode");
    press(&s, 100); sim_ms(&s, 400);
    CHECK(s.c.mode == MODE_LOOK, "single press -> look (%d)", s.c.mode);

    /* hold 1 s from move: inventory, E tapped once */
    press(&s, 100); sim_ms(&s, 400);
    sim_reset_counts(&s);
    s.pressed = 1;
    sim_ms(&s, 1050);
    CHECK(controller_leds(&s.c, s.now) == 0xF, "all four flash at 1 s");
    sim_ms(&s, 100);
    s.pressed = 0;
    sim_ms(&s, 300);
    CHECK(s.c.mode == MODE_INVENTORY, "hold -> inventory (%d)", s.c.mode);
    CHECK(s.key_e_presses == 1, "E tapped once (%d)", s.key_e_presses);
    CHECK(s.left_presses == 0, "no click from the hold");
    CHECK(controller_leds(&s.c, s.now) == LED_BLUE, "blue in inventory");

    /* click in the inventory */
    sim_reset_counts(&s);
    press(&s, 80); sim_ms(&s, 100);
    CHECK(s.left_presses == 1, "press in inventory = one left click (%d)", s.left_presses);

    /* hold again: back to move, E again */
    sim_reset_counts(&s);
    press(&s, 1200); sim_ms(&s, 300);
    CHECK(s.c.mode == MODE_MOVE, "closes back to the last play mode (move) (%d)", s.c.mode);
    CHECK(s.key_e_presses == 1, "E tapped to close (%d)", s.key_e_presses);
    CHECK(s.left_presses == 0, "closing never clicks (%d)", s.left_presses);

    /* resync: 3 s hold flips to inventory without E */
    sim_reset_counts(&s);
    s.pressed = 1;
    sim_ms(&s, 3100);
    uint8_t l = controller_leds(&s.c, s.now);
    CHECK(l == LED_ORANGE || l == LED_RED || l == LED_BLUE || l == LED_GREEN, "one LED spinning at 3 s (%x)", l);
    s.pressed = 0;
    sim_ms(&s, 100);
    CHECK(s.c.mode == MODE_INVENTORY, "resync -> inventory (%d)", s.c.mode);
    CHECK(s.key_e_presses == 0, "resync sends no E (%d)", s.key_e_presses);
    /* ~80 ms after letting go: inside the first blink, blue off */
    CHECK(controller_leds(&s.c, s.now) == 0, "blink 1 off (%x)", controller_leds(&s.c, s.now));
    sim_ms(&s, 150);
    CHECK(controller_leds(&s.c, s.now) == LED_BLUE, "blink 1 on");
    sim_ms(&s, 1000);
    CHECK(controller_leds(&s.c, s.now) == LED_BLUE, "steady blue after 3 blinks");

    /* double press in play = Shift tap */
    press(&s, 1200); sim_ms(&s, 300);   /* back to move */
    sim_reset_counts(&s);
    press(&s, 80); sim_ms(&s, 100); press(&s, 80); sim_ms(&s, 400);
    CHECK(s.shift_presses == 1, "double press taps Shift once (%d)", s.shift_presses);
    CHECK(s.c.mode == MODE_MOVE, "double press keeps the mode (%d)", s.c.mode);
    CHECK(s.c.play_mode == MODE_MOVE, "play mode remembered");
}

static void test_air_mouse(void)
{
    sim_t s;
    boot(&s);
    sim_reset_counts(&s);
    /* turn left around the vertical (+y is down, so "up" is -y): 30 deg/s for 1 s */
    s.gyro_dps[1] = -30.0f;
    sim_ms(&s, 1000);
    s.gyro_dps[1] = 0.0f;
    sim_ms(&s, 200);
    float expect = 30.0f * (1.0f - POINT_DEADZONE_DPS / 30.0f) * LOOK_COUNTS_PER_DEG;   /* 270 */
    CHECK(fabsf(-(float)s.sum_dx - expect) < 0.05f * expect, "turn left 30 deg -> dx %ld (want about -%.0f)",
          s.sum_dx, expect);
    CHECK(labs(s.sum_dy) < 10, "no up/down from a pure turn (dy %ld)", s.sum_dy);

    /* tip forward (point down) around z */
    sim_reset_counts(&s);
    s.gyro_dps[2] = 20.0f;
    sim_ms(&s, 500);
    s.gyro_dps[2] = 0.0f;
    sim_ms(&s, 200);
    expect = 10.0f * (1.0f - POINT_DEADZONE_DPS / 20.0f) * LOOK_COUNTS_PER_DEG;   /* 85 */
    CHECK(fabsf((float)s.sum_dy - expect) < 0.1f * expect, "point down 10 deg -> dy %ld (want about %.0f)",
          s.sum_dy, expect);

    /* hand shake below the dead zone: nothing */
    sim_reset_counts(&s);
    for (int k = 0; k < 20; k++) {
        s.gyro_dps[1] = (k % 2) ? 2.0f : -2.0f;
        sim_ms(&s, 50);
    }
    s.gyro_dps[1] = 0;
    sim_ms(&s, 100);
    CHECK(s.sum_dx == 0 && s.sum_dy == 0, "shake inside the dead zone -> no motion (%ld, %ld)", s.sum_dx, s.sum_dy);

    /* a fast swing freezes the camera, including the 60 ms before it */
    sim_reset_counts(&s);
    s.gyro_dps[1] = -100.0f; sim_ms(&s, 40);     /* wind-up just under the freeze speed */
    s.gyro_dps[1] = -400.0f; sim_ms(&s, 150);    /* the swing */
    s.gyro_dps[1] = 0.0f;    sim_ms(&s, 300);
    CHECK(labs(s.sum_dx) < 60, "fast swing barely moves the camera (dx %ld; 40 ms at 100 deg/s would be %d)",
          s.sum_dx, (int)(0.04f * 97 * LOOK_COUNTS_PER_DEG));

    /* mining holds the camera still */
    sim_reset_counts(&s);
    controller_gesture(&s.c, GEV_MINE_START, s.now);
    s.gyro_dps[1] = -30.0f; sim_ms(&s, 500);
    s.gyro_dps[1] = 0.0f;
    controller_gesture(&s.c, GEV_MINE_STOP, s.now);
    sim_ms(&s, 200);
    CHECK(s.sum_dx == 0, "camera frozen while mining (dx %ld)", s.sum_dx);

    /* inventory: slower */
    boot(&s);
    press(&s, 1200); sim_ms(&s, 300);
    sim_reset_counts(&s);
    s.gyro_dps[1] = -30.0f; sim_ms(&s, 1000);
    s.gyro_dps[1] = 0.0f;   sim_ms(&s, 200);
    expect = 27.0f * INV_COUNTS_PER_DEG;
    CHECK(fabsf(-(float)s.sum_dx - expect) < 0.05f * expect, "inventory cursor: dx %ld (want about -%.0f)", s.sum_dx,
          expect);
}

static void test_gestures(void)
{
    sim_t s;
    boot(&s);
    sim_reset_counts(&s);
    controller_gesture(&s.c, GEV_SWORD, s.now);
    sim_ms(&s, 50);
    CHECK(s.left_presses == 1 && s.left_held_ms >= TAP_MS - 2 && s.left_held_ms <= TAP_MS + 1,
          "sword = one short left click (%d, %d ms)", s.left_presses, s.left_held_ms);
    CHECK(controller_leds(&s.c, s.now) == (LED_GREEN | LED_RED), "red blinks with a gesture");
    sim_ms(&s, 100);
    CHECK(controller_leds(&s.c, s.now) == LED_GREEN, "red blink ends");

    controller_gesture(&s.c, GEV_PLACE, s.now);
    controller_gesture(&s.c, GEV_SWAP_LEFT, s.now);
    sim_ms(&s, 50);
    controller_gesture(&s.c, GEV_SWAP_RIGHT, s.now);
    controller_gesture(&s.c, GEV_SWAP_RIGHT, s.now);
    sim_ms(&s, 50);
    CHECK(s.right_presses == 1, "place = right click (%d)", s.right_presses);
    CHECK(s.sum_wheel == -1, "swaps: +1 -2 = %ld", s.sum_wheel);

    /* mine holds until stop, and a mode change lets go */
    sim_reset_counts(&s);
    controller_gesture(&s.c, GEV_MINE_START, s.now);
    sim_ms(&s, 1000);
    CHECK(s.left_held_ms >= 999, "mine holds left (%d ms)", s.left_held_ms);
    press(&s, 100); sim_ms(&s, 400);
    CHECK(s.c.mode == MODE_MOVE && !(s.last.buttons & MOUSE_LEFT), "switching to move lets go of mining");

    /* in move mode gestures do nothing */
    sim_reset_counts(&s);
    controller_gesture(&s.c, GEV_SWORD, s.now);
    controller_gesture(&s.c, GEV_MINE_START, s.now);
    controller_gesture(&s.c, GEV_SWAP_LEFT, s.now);
    sim_ms(&s, 100);
    CHECK(s.left_presses == 0 && s.sum_wheel == 0, "no gestures in move mode");
}

/* tilt the board around the pitch axis (z) by `deg` over 0.5 s */
static void tilt(sim_t *s, int axis, float deg)
{
    s->gyro_dps[axis] = deg / 0.5f;
    sim_ms(s, 500);
    s->gyro_dps[axis] = 0.0f;
}

static void test_move_mode(void)
{
    sim_t s;
    boot(&s);
    press(&s, 100); sim_ms(&s, 600);       /* -> move, zeroed */
    CHECK(s.c.mode == MODE_MOVE && s.c.walk.zeroed, "move mode zeroed");

    sim_reset_counts(&s);
    tilt(&s, PITCH_AXIS, 20.0f);
    sim_ms(&s, 1000);
    CHECK(s.w_held_ms > 900 && s.ctrl_held_ms == 0, "20 deg forward = W, no sprint (W %d ms, Ctrl %d ms, fwd %.1f)",
          s.w_held_ms, s.ctrl_held_ms, s.c.walk.fwd_deg);

    sim_reset_counts(&s);
    tilt(&s, PITCH_AXIS, 25.0f);           /* now 45 deg */
    sim_ms(&s, 1000);
    CHECK(s.ctrl_held_ms > 500, "45 deg held = sprint (Ctrl %d ms, fwd %.1f)", s.ctrl_held_ms, s.c.walk.fwd_deg);
    sim_reset_counts(&s);
    tilt(&s, PITCH_AXIS, -20.0f);          /* ease off to 25 deg: still sprinting */
    sim_ms(&s, 500);
    CHECK(s.ctrl_held_ms >= 900, "sprint stays on above the walk angle (Ctrl %d ms)", s.ctrl_held_ms);
    tilt(&s, PITCH_AXIS, -20.0f);          /* 5 deg: stop */
    sim_ms(&s, 500);
    CHECK(!(s.last.modifier & MOD_LCTRL) && !has_key(&s.last, KEY_W), "back near level: W and Ctrl let go (fwd %.1f)",
          s.c.walk.fwd_deg);

    sim_reset_counts(&s);
    tilt(&s, PITCH_AXIS, -25.0f);          /* -20 deg: back */
    sim_ms(&s, 500);
    CHECK(s.s_held_ms > 400 && s.w_held_ms == 0, "tip back = S (%d ms)", s.s_held_ms);
    tilt(&s, PITCH_AXIS, 20.0f);

    sim_reset_counts(&s);
    tilt(&s, 0, 30.0f);                    /* side tilt around x */
    sim_ms(&s, 500);
    int side1 = s.a_held_ms > 400 ? 'A' : (s.d_held_ms > 400 ? 'D' : '-');
    tilt(&s, 0, -60.0f);
    sim_reset_counts(&s);
    sim_ms(&s, 500);
    int side2 = s.a_held_ms > 400 ? 'A' : (s.d_held_ms > 400 ? 'D' : '-');
    CHECK(side1 != '-' && side2 != '-' && side1 != side2, "side tilts give A and D (%c then %c, side %.1f)", side1,
          side2, s.c.walk.side_deg);
    CHECK(s.w_held_ms == 0 && s.s_held_ms == 0, "side tilt doesn't walk forward/back");
    tilt(&s, 0, 30.0f);

    /* steering: turning left moves the camera, pointing up/down doesn't */
    sim_reset_counts(&s);
    s.gyro_dps[1] = -30.0f; sim_ms(&s, 500); s.gyro_dps[1] = 0; sim_ms(&s, 200);
    CHECK(s.sum_dx < -50, "steering in move mode (dx %ld)", s.sum_dx);

    /* jump flick */
    sim_reset_counts(&s);
    for (int i = 0; i < 3; i++) s.extra_g[i] = -1.2f * (i == UPRIGHT_AXIS);   /* up is -y */
    sim_ms(&s, 60);
    memset(s.extra_g, 0, sizeof(s.extra_g));
    sim_ms(&s, 200);
#if MOVE_JUMP_ENABLED
    CHECK(s.space_presses == 1, "upward flick = one Space (%d)", s.space_presses);
#endif
    /* leaving move mode lets go of the keys */
    tilt(&s, PITCH_AXIS, 20.0f);
    sim_ms(&s, 200);
    press(&s, 100); sim_ms(&s, 400);
    CHECK(s.c.mode == MODE_LOOK && !has_key(&s.last, KEY_W), "switching to look lets go of W");
}

static void test_rest_pose(void)
{
    sim_t s;
    boot(&s);
    /* lay it down flat: "up" moves from -y to z */
    s.gyro_dps[0] = -90.0f / 0.5f;
    sim_ms(&s, 500);
    s.gyro_dps[0] = 0;
    sim_ms(&s, 1300);
    CHECK(s.c.paused, "flat and still for 1 s -> paused (up %.2f %.2f %.2f)", s.c.motion.up[0], s.c.motion.up[1],
          s.c.motion.up[2]);
    sim_reset_counts(&s);
    s.gyro_dps[1] = 10.0f;      /* slow drift below the wake speed */
    sim_ms(&s, 300);
    s.gyro_dps[1] = 0;
    CHECK(s.sum_dx == 0 && s.sum_dy == 0, "no motion out while paused");
    int lit = 0;
    for (int i = 0; i < 2000; i++) lit += controller_leds(&s.c, s.now + i) != 0;
    CHECK(lit > 200 && lit < 1800, "LED breathes while paused (%d of 2000 ms lit)", lit);
    s.gyro_dps[0] = 90.0f / 0.5f;  /* pick it up */
    sim_ms(&s, 500);
    s.gyro_dps[0] = 0;
    sim_ms(&s, 50);
    CHECK(!s.c.paused, "picking it up wakes it");
}

int main(void)
{
    test_calibration();
    test_calibration_in_hand();
    test_button_timing();
    test_modes();
    test_air_mouse();
    test_gestures();
    test_move_mode();
    test_rest_pose();
    printf("%d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
