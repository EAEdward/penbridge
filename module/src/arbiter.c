#include "penbridge.h"

/* The whole gesture policy lives here.
 *
 * Stock firmware: one squeeze is one KEY_F19 press pair, and the host app flips
 * the tool once per press pair. So a squeeze has to be answered with one
 * complete pair at DOWN (pen -> eraser) and one at UP (eraser -> pen).
 *
 * Two properties have to be preserved while doing that:
 *   - a repeat DOWN (value 2) is not a new edge, and a DOWN on a press we already
 *     own cannot be a second squeeze;
 *   - an UP with no owned DOWN behind it is an orphan and is dropped, never
 *     forwarded, or the tool would flip on its own.
 *
 * The wanted tool is a level, not a count: eraser while the squeeze is held,
 * pen while it is not. Each complete pair is written to the virtual keyboard in
 * a single batch, so a synthetic key is never left held.
 */
void arbiter_init(struct pb_arbiter *a)
{
    a->owned = 0;
    a->model_eraser = 0;
    a->next_ok = 0;
}

/* Emit one pair if the app is showing the wrong tool and its switch deadline has
 * passed. Reaching the wanted tool only ever needs one backwards flip. */
static struct pb_decision settle(struct pb_arbiter *a, long long now)
{
    struct pb_decision d = { 0, 0 };
    if (a->owned == a->model_eraser)
        return d;
    if (now < a->next_ok)
        return d;
    a->model_eraser = a->owned;
    a->next_ok = now + PB_TOGGLE_SPACING_MS;
    d.toggle = 1;
    return d;
}

struct pb_decision arbiter_settle(struct pb_arbiter *a, long long now)
{
    return settle(a, now);
}

struct pb_decision arbiter_feed(struct pb_arbiter *a, const struct pb_event *ev, long long now)
{
    struct pb_decision d = { 0, 0 };

    if (ev->type == PB_EV_SYN) {
        /* The kernel dropped events: pairing state after a gap is unreliable. */
        if (ev->code == PB_SYN_DROPPED)
            a->owned = 0;
        d.forward = 1;
        return d;
    }
    if (ev->type != PB_EV_KEY || ev->code != PB_KEY_F19)
        return (struct pb_decision){ 1, 0 };

    if (ev->value == 2)
        return d;
    if (ev->value == 1) {
        if (a->owned)
            return d;
        a->owned = 1;
        return settle(a, now);
    }
    if (!a->owned)
        return d;
    a->owned = 0;
    return settle(a, now);
}

/* When the wanted tool is not reached yet, the app's switch deadline is the only
 * thing the main loop has to wake up for. */
long long arbiter_deadline(const struct pb_arbiter *a)
{
    return a->owned == a->model_eraser ? -1 : a->next_ok;
}
