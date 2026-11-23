/* Every number you might want to tune on the board, in one place.
 *
 * Values marked "tune" are starting guesses from docs/controls.md. None of
 * them has been tried on real hardware yet (Nov 22): change them after the
 * first play test and write down what worked in docs/results.md.
 *
 * Axes are the MPU-6050's own axes. From our recordings (board held upright,
 * as in play): gravity sits on -y (the accelerometer reads about -1 g on y),
 * chopping (mine) is a rotation around z, the hotbar flick around x.
 */
#pragma once

/* ---- sensor units (docs/architecture.md "Sensor units") ---- */
#define ACCEL_COUNTS_PER_G    2048.0f   /* +-16 g */
#define GYRO_COUNTS_PER_DPS   16.4f     /* +-2000 deg/s */
#define SAMPLE_HZ             100
#define SAMPLE_DT             (1.0f / SAMPLE_HZ)

/* ---- axes ---- */
#define UPRIGHT_AXIS          1      /* y: the long axis of the handle (gravity is on it when upright) */
#define PITCH_AXIS            2      /* z: rotate around this to point up/down (and to tilt forward) */
#define PITCH_SIGN            (+1.0f)/* tune: flip if looking up/down or walking forward/back is reversed */
#define YAW_SIGN              (+1.0f)/* tune: flip if turning left moves the camera right */
#define SIDE_SIGN             (+1.0f)/* tune: flip if A and D are swapped in move mode */

/* ---- power-on gyro calibration ---- */
#define CAL_SAMPLES           150    /* 1.5 s of keeping still (on a table) */
#define CAL_STILL_DPS         3.0f   /* each gyro axis must stay within this range (max - min) */
#define CAL_GIVE_UP_MS        3000   /* never that still (held in a hand)? after this take the calmest second so far */
#define CAL_BLOCK_SAMPLES     100    /* ... measured in blocks of 1 s */
#define CAL_FALLBACK_MAX_DPS  15.0f  /* ... but not while it is being waved about: keep waiting for a calmer second */

/* ---- gravity estimate (gyro-corrected low-pass of the accelerometer) ---- */
#define GRAVITY_ACCEL_WEIGHT  0.02f  /* per sample; 0.02 at 100 Hz ~ 0.5 s time constant */

/* ---- pointing: look mode and inventory (Wii-remote style, rate -> mouse) ---- */
#define LOOK_COUNTS_PER_DEG   10.0f  /* tune: mouse counts per degree turned. At Minecraft's default
                                        sensitivity one count is ~0.15 deg, so 10 turns the camera
                                        ~1.5x as far as the board */
#define INV_COUNTS_PER_DEG    6.0f   /* tune: inventory cursor, lower so slots are easy to hit */
#define MOVE_COUNTS_PER_DEG   10.0f  /* tune: left/right steering in move mode */
#define POINT_DEADZONE_DPS    3.0f   /* tune: slower than this = hand shake, ignored */
#ifndef FREEZE_DPS
#define FREEZE_DPS            250.0f /* tune: faster than this = probably a gesture, camera freezes */
#endif
#define FREEZE_HOLD_MS        150    /* camera stays frozen this long after the fast motion ends */
#define POINT_DELAY_SAMPLES   6      /* mouse motion is held back 60 ms so a starting gesture can cancel it */

/* ---- move mode (PR #11 is on hold: everything move-mode lives here so it is easy to change) ---- */
#define MOVE_ZERO_MS          200    /* hold still this long after entering move mode; neutral is saved */
#define WALK_DEG              15.0f  /* tune: forward/back tilt for W / S */
#define STRAFE_DEG            22.0f  /* tune: side tilt for A / D */
#define TILT_HYST_DEG         3.0f   /* let go a few degrees below the threshold so keys don't chatter */
#define SPRINT_DEG            40.0f  /* tune: steep forward tilt ... */
#define SPRINT_HOLD_MS        300    /* ... held this long starts sprinting; stops below WALK_DEG */
#define MOVE_JUMP_ENABLED     1      /* 1 = quick upward flick taps Space in move mode (controls.md, PR #11) */
#define JUMP_UP_G             0.8f   /* tune: upward acceleration beyond gravity that counts as a flick */
#define JUMP_COOLDOWN_MS      600

/* ---- rest pose: set down flat and still -> pause all output ---- */
#define REST_FLAT_MAX         0.5f   /* "flat" = less than this much of gravity on the handle axis */
#define REST_STILL_DPS        5.0f
#define REST_AFTER_MS         1000
#define REST_WAKE_DPS         20.0f  /* moving faster than this wakes it up */

/* ---- button (blue, PA0) ---- */
#define BTN_DEBOUNCE_MS       20
#define BTN_SHORT_MS          400    /* released before this = a press */
#define BTN_DOUBLE_GAP_MS     300    /* second press must start within this after the first release */
#define BTN_HOLD_MS           1000   /* inventory */
#define BTN_RESYNC_MS         3000   /* resync */

/* ---- taps: how long a key or click is held for one "press" ---- */
#define TAP_MS                30

/* ---- LEDs ---- */
#define GESTURE_BLINK_MS      100
#define SPIN_STEP_MS          100
