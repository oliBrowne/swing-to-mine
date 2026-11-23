#include "controller.h"

#include <string.h>

#include "config.h"

#define BLINK_HALF_MS 150

static int before(uint32_t now, uint32_t t) { return (int32_t)(now - t) < 0; }

static uint8_t mode_led(ctrl_mode_t m)
{
    switch (m) {
    case MODE_LOOK:      return LED_GREEN;
    case MODE_MOVE:      return LED_ORANGE;
    case MODE_INVENTORY: return LED_BLUE;
    default:             return 0;
    }
}

static void tap(controller_t *c, int which, uint32_t now)
{
    c->tap_on[which] = 1;
    c->tap_until[which] = now + TAP_MS;
}

static void blink(controller_t *c, uint8_t mask, int count, uint32_t now)
{
    c->blink_mask = mask;
    c->blink_count = count;
    c->blink_start = now;
}

/* Let go of everything that is held: walking keys, sprint, mining, queued
 * mouse motion. Taps already started still finish (they are 30 ms). */
static void release_all(controller_t *c)
{
    c->mining = 0;
    c->walk.keys = 0;
    c->dx = c->dy = 0;
    c->wheel = 0;
    air_mouse_reset(&c->air);
}

static void set_mode(controller_t *c, ctrl_mode_t m, uint32_t now)
{
    release_all(c);
    c->mode = m;
    if (m == MODE_LOOK || m == MODE_MOVE) c->play_mode = m;
    if (m == MODE_MOVE) tilt_walk_enter(&c->walk, now);
}

void controller_init(controller_t *c, uint32_t now)
{
    memset(c, 0, sizeof(*c));
    c->mode = MODE_CALIBRATE;
    c->play_mode = MODE_LOOK;
    motion_init(&c->motion, now);
    air_mouse_reset(&c->air);
    button_init(&c->button);
}

void controller_tick(controller_t *c, int pressed, uint32_t now)
{
    for (int i = 0; i < TAP_COUNT; i++)
        if (c->tap_on[i] && !before(now, c->tap_until[i])) c->tap_on[i] = 0;

    button_event_t ev = button_update(&c->button, pressed, c->mode == MODE_INVENTORY, now);
    if (c->mode == MODE_CALIBRATE || ev == BTN_NONE) return;
    if (c->paused) {           /* resting: all output is paused, the button too */
        c->n_buttons_ignored++;
        return;
    }

    int in_play = c->mode != MODE_INVENTORY;
    switch (ev) {
    case BTN_SINGLE:      /* look <-> move */
        if (in_play) set_mode(c, c->mode == MODE_LOOK ? MODE_MOVE : MODE_LOOK, now);
        break;
    case BTN_DOUBLE:      /* crouch: Minecraft's Sneak must be set to Toggle */
        if (in_play) {
            tap(c, TAP_SHIFT, now);
            blink(c, mode_led(c->mode), 2, now);
        }
        break;
    case BTN_CLICK:       /* inventory: pick up / drop */
        if (!in_play) tap(c, TAP_LEFT, now);
        break;
    case BTN_HOLD:        /* open / close the inventory */
        set_mode(c, in_play ? MODE_INVENTORY : c->play_mode, now);
        tap(c, TAP_E, now);
        break;
    case BTN_RESYNC:      /* same switch, no key: the game already did it */
        set_mode(c, in_play ? MODE_INVENTORY : c->play_mode, now);
        blink(c, mode_led(c->mode), 3, now);
        break;
    default:
        break;
    }
}

static void update_rest(controller_t *c, uint32_t now)
{
    const motion_t *m = &c->motion;
    float on_axis = m->up[UPRIGHT_AXIS];
    if (on_axis < 0) on_axis = -on_axis;
    int flat = on_axis < REST_FLAT_MAX;
    if (c->paused) {
        if (!flat || m->gyro_mag > REST_WAKE_DPS) {
            c->paused = 0;
            c->rest_since = 0;
        }
        return;
    }
    if (flat && m->gyro_mag < REST_STILL_DPS) {
        if (!c->rest_since) c->rest_since = now ? now : 1;
        if (now - c->rest_since >= REST_AFTER_MS) {
            c->paused = 1;
            release_all(c);
        }
    } else {
        c->rest_since = 0;
    }
}

void controller_sample(controller_t *c, const int16_t raw[6], uint32_t now)
{
    if (motion_update(&c->motion, raw, now)) set_mode(c, MODE_LOOK, now);
    if (c->mode == MODE_CALIBRATE) return;

    update_rest(c, now);
    if (c->paused) return;

    switch (c->mode) {
    case MODE_LOOK:
        air_mouse_update(&c->air, &c->motion, LOOK_COUNTS_PER_DEG, 1, c->mining, now, &c->dx, &c->dy);
        break;
    case MODE_INVENTORY:
        air_mouse_update(&c->air, &c->motion, INV_COUNTS_PER_DEG, 1, 0, now, &c->dx, &c->dy);
        break;
    case MODE_MOVE: {
        int jump = 0;
        if (tilt_walk_update(&c->walk, &c->motion, now, &jump)) blink(c, LED_ORANGE, 1, now);
        if (jump) {
            tap(c, TAP_SPACE, now);
            c->n_jumps++;
        }
        /* steer left/right only; no camera until neutral is saved */
        if (c->walk.zeroed)
            air_mouse_update(&c->air, &c->motion, MOVE_COUNTS_PER_DEG, 0, 0, now, &c->dx, &c->dy);
        break;
    }
    default:
        break;
    }
}

void controller_gesture(controller_t *c, gesture_event_t ev, uint32_t now)
{
    if (ev == GEV_NONE) return;
    if (ev == GEV_MINE_STOP) {     /* always safe to let go */
        c->mining = 0;
        return;
    }
    if (c->mode != MODE_LOOK || c->paused) {   /* gestures only work in look mode */
        c->n_gestures_ignored++;
        return;
    }
    switch (ev) {
    case GEV_SWORD:      tap(c, TAP_LEFT, now); break;
    case GEV_PLACE:      tap(c, TAP_RIGHT, now); break;
    case GEV_SWAP_LEFT:  c->wheel += 1; break;   /* scroll up = slot to the left */
    case GEV_SWAP_RIGHT: c->wheel -= 1; break;
    case GEV_MINE_START: c->mining = 1; break;
    default: break;
    }
    c->n_gestures_used++;
    c->red_until = now + GESTURE_BLINK_MS;
}

static void add_key(hid_frame_t *f, uint8_t key)
{
    for (int i = 0; i < 6; i++) {
        if (f->keys[i] == 0) {
            f->keys[i] = key;
            return;
        }
    }
}

void controller_frame(controller_t *c, uint32_t now, hid_frame_t *f)
{
    (void)now;
    memset(f, 0, sizeof(*f));
    if (c->mode == MODE_MOVE && !c->paused) {
        uint8_t k = c->walk.keys;
        if (k & WALK_W) add_key(f, KEY_W);
        if (k & WALK_A) add_key(f, KEY_A);
        if (k & WALK_S) add_key(f, KEY_S);
        if (k & WALK_D) add_key(f, KEY_D);
        if (k & WALK_SPRINT) f->modifier |= MOD_LCTRL;
    }
    if (c->tap_on[TAP_E]) add_key(f, KEY_E);
    if (c->tap_on[TAP_SPACE]) add_key(f, KEY_SPACE);
    if (c->tap_on[TAP_SHIFT]) f->modifier |= MOD_LSHIFT;
    if (c->tap_on[TAP_LEFT] || c->mining) f->buttons |= MOUSE_LEFT;
    if (c->tap_on[TAP_RIGHT]) f->buttons |= MOUSE_RIGHT;
    f->dx = c->dx;
    f->dy = c->dy;
    f->wheel = c->wheel;
    c->dx = c->dy = 0;
    c->wheel = 0;
}

static uint8_t spin(uint32_t now)
{
    static const uint8_t ring[4] = {LED_ORANGE, LED_RED, LED_BLUE, LED_GREEN};  /* clockwise */
    return ring[(now / SPIN_STEP_MS) % 4];
}

uint8_t controller_leds(const controller_t *c, uint32_t now)
{
    if (c->mode == MODE_CALIBRATE) return spin(now);

    uint8_t leds = mode_led(c->mode);
    uint32_t held = c->button.held_ms;
    if (held >= BTN_RESYNC_MS) return spin(now);
    if (held >= BTN_HOLD_MS && held < BTN_HOLD_MS + 200) return 0xF;   /* flash once at 1 s */

    uint32_t phase = (now - c->blink_start) / BLINK_HALF_MS;   /* off, on, off, on, ... */
    if (phase < (uint32_t)(2 * c->blink_count)) {
        if ((phase % 2) == 0) leds &= (uint8_t)~c->blink_mask;
    } else if (c->paused) {
        /* breathe: software PWM, 10 ms period, brightness up and down over 2 s */
        uint32_t t = now % 2000;
        uint32_t level = t < 1000 ? t / 100 : (1999 - t) / 100;     /* 0..9 */
        if (now % 10 >= level) leds = 0;
    }
    if (before(now, c->red_until)) leds |= LED_RED;
    return leds;
}
