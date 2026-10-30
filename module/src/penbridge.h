/* PenBridge - shared kernel ABI and module interfaces.
 *
 * Design reference: WondersReady/FocusPen (docs/DESIGN.md, docs/INPUT_ROUTING.md).
 * Same three rules, transposed to an evdev daemon:
 *   1. one ownership decision at the earliest entry (EVIOCGRAB on the pen node);
 *   2. an owned DOWN and its UP are consumed as a pair, never half of one;
 *   3. every replay is a complete press+release pair, so no synthetic key can
 *      ever be left held (the "release safeguard" that keeps a tool switch from
 *      sticking).
 */
#ifndef PENBRIDGE_H
#define PENBRIDGE_H

#include <stddef.h>
#include <stdint.h>

/* Kernel ABI (aarch64): struct input_event is 24 bytes. */
struct pb_timeval { long tv_sec; long tv_usec; };
struct pb_event {
    struct pb_timeval time;
    uint16_t type;
    uint16_t code;
    int32_t value;
};

#define PB_EV_SYN 0x00
#define PB_EV_KEY 0x01
#define PB_SYN_REPORT 0
#define PB_SYN_DROPPED 3
#define PB_KEY_F19 189
#define PB_KEY_MAX 0x2ff

/* The host app needs this much between two tool switches; below it the second
 * toggle is discarded and the tool sticks. Measured on StarNote, see DESIGN.md. */
#define PB_TOGGLE_SPACING_MS 300

void pb_log(const char *fmt, ...);

/* pen_device.c - locate the pen keyboard interface that owns KEY_F19. */
int pen_open(char *path, size_t cap);

/* replay.c - the virtual keyboard the arbitrated keys are replayed through. */
int replay_open(int src_fd);
void replay_close(int fd);
int replay_pair(int fd, unsigned code);
int replay_forward(int fd, const struct pb_event *ev);
int replay_present(void);

/* arbiter.c - one squeeze edge in, one complete press pair owed out. */
struct pb_arbiter {
    int owned;         /* the physical squeeze is currently held */
    int model_eraser;  /* our model of the tool the app is showing */
    long long next_ok; /* earliest instant the app accepts another toggle */
};

struct pb_decision {
    int forward; /* hand the untouched event to the system */
    int toggle;  /* replay one complete press pair now */
};

void arbiter_init(struct pb_arbiter *a);
struct pb_decision arbiter_feed(struct pb_arbiter *a, const struct pb_event *ev, long long now);
struct pb_decision arbiter_settle(struct pb_arbiter *a, long long now);
long long arbiter_deadline(const struct pb_arbiter *a);

#endif
