/* Replays a recorded session (data/raw/<person>/<session>.csv) through the
 * controller in look mode, as if the board were plugged in, and says how
 * much the camera would move during each kind of labelled motion.
 * No model here: this checks the air mouse (dead zone, freeze on fast motion).
 *
 *   make -C firmware/swing_to_mine/test replay
 *   ./replay ../../../data/raw/P10/s41.csv
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "controller.h"

#define MAX_LABELS 2000
typedef struct { long start, end; char name[32]; } label_t;
static label_t labels[MAX_LABELS];
static int n_labels;

typedef struct { double travel; long ms, frozen_ms; } stat_t;
#define MAX_KINDS 16
static char kind_name[MAX_KINDS][32];
static stat_t kind_stat[MAX_KINDS];
static int n_kinds;

static int kind_of(long t)
{
    const char *name = "between cues";
    for (int i = 0; i < n_labels; i++)
        if (t >= labels[i].start && t <= labels[i].end) { name = labels[i].name; break; }
    for (int k = 0; k < n_kinds; k++)
        if (!strcmp(kind_name[k], name)) return k;
    if (n_kinds == MAX_KINDS) return 0;
    snprintf(kind_name[n_kinds], sizeof(kind_name[0]), "%s", name);
    return n_kinds++;
}

static void load_labels(const char *csv)
{
    char path[1024];
    snprintf(path, sizeof(path), "%.*s.labels.csv", (int)(strlen(csv) - 4), csv);
    FILE *f = fopen(path, "r");
    if (!f) return;
    char line[256];
    while (fgets(line, sizeof(line), f) && n_labels < MAX_LABELS) {
        label_t l;
        if (sscanf(line, "%ld,%ld,%31s", &l.start, &l.end, l.name) == 3) labels[n_labels++] = l;
    }
    fclose(f);
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: replay <session.csv>\n");
        return 2;
    }
    FILE *f = fopen(argv[1], "r");
    if (!f) { perror(argv[1]); return 1; }
    load_labels(argv[1]);

    static controller_t c;
    char line[256];
    long t0 = -1, prev = -1, n = 0;
    double total_dx = 0, total_dy = 0;
    while (fgets(line, sizeof(line), f)) {
        long t; int v[6];
        if (sscanf(line, "%ld,%d,%d,%d,%d,%d,%d", &t, &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) != 7) continue;
        if (t0 < 0) {
            t0 = t;
            controller_init(&c, 1);
            /* the recordings don't start still, so skip calibration: trust a zero bias */
            c.motion.calibrated = 1;
            c.mode = MODE_LOOK;
        }
        if (prev >= 0 && (t <= prev || t - prev > 1000)) { prev = t; continue; }   /* board restarts */
        int16_t raw[6];
        for (int i = 0; i < 6; i++) raw[i] = (int16_t)v[i];
        uint32_t now = (uint32_t)(t - t0 + 1);
        controller_sample(&c, raw, now);
        controller_tick(&c, 0, now);
        hid_frame_t fr;
        controller_frame(&c, now, &fr);
        int k = kind_of(t);
        long dt = prev >= 0 ? t - prev : 10;
        kind_stat[k].travel += sqrt((double)fr.dx * fr.dx + (double)fr.dy * fr.dy);
        kind_stat[k].ms += dt;
        if (c.air.frozen) kind_stat[k].frozen_ms += dt;
        total_dx += fr.dx;
        total_dy += fr.dy;
        prev = t;
        n++;
    }
    fclose(f);
    printf("%s: %ld samples, %.1f min. Look mode, gain %.0f counts/deg, dead zone %.0f deg/s, freeze above %.0f deg/s\n",
           argv[1], n, (prev - t0) / 60000.0, LOOK_COUNTS_PER_DEG, POINT_DEADZONE_DPS, FREEZE_DPS);
    printf("  %-14s %8s %16s %10s\n", "label", "seconds", "camera deg/s", "frozen");
    for (int k = 0; k < n_kinds; k++) {
        stat_t *s = &kind_stat[k];
        if (!s->ms) continue;
        /* counts -> degrees of camera at Minecraft's default sensitivity (~0.15 deg/count) */
        printf("  %-14s %8.1f %16.1f %9.0f%%\n", kind_name[k], s->ms / 1000.0,
               s->travel * 0.15 / (s->ms / 1000.0), 100.0 * s->frozen_ms / s->ms);
    }
    printf("  net drift: dx %.0f, dy %.0f counts\n", total_dx, total_dy);
    return 0;
}
