#ifndef STARPOST_PAD_H
#define STARPOST_PAD_H

#include "game/game.h"

/* Stick deflection inside this magnitude does not turn.
   Triggers above this count as pressed. */
#define PAD_DEADZONE 0.35f
#define PAD_TRIGGER 0.50f
/* Drop a latched direction only once the stick returns inside this. */
#define PAD_RELEASE 0.20f
/* Axis, sensor, and hotplug events past this stay queued for the next frame. */
#define STARPOST_MAX_PAD_EVENTS 64

typedef struct {
    float stick_x;
    int dpad_left;
    int dpad_right;
    int a;
    int b;
    int x;
    int y;
    int rb;
    float rt;
    float lt;
    int start;
} PadState;

/* OR gamepad edges into an existing keyboard frame. One axis only:
   left and right together produce neither. */
void pad_apply(InputFrame *io, const PadState *pad);
/* Latch a stick that has crossed the deadzone until it returns to PAD_RELEASE.
   Near-threshold chatter must not look like a fresh press every sample. */
void pad_stick_hold(float stick_x, int *hold_left, int *hold_right);
/* How many queued pad events this frame may drain. */
int pad_event_budget(int queued);

#endif
