#ifndef STARPOST_PAD_H
#define STARPOST_PAD_H

#include "game/game.h"

/* Stick deflection inside this magnitude does not turn.
   Triggers above this count as pressed. */
#define PAD_DEADZONE 0.35f
#define PAD_TRIGGER 0.50f

typedef struct {
    float stick_x;
    int dpad_left;
    int dpad_right;
    int a;
    int b;
    float rt;
    float lt;
    int start;
} PadState;

/* OR gamepad edges into an existing keyboard frame. One axis only:
   left and right together produce neither. */
void pad_apply(InputFrame *io, const PadState *pad);

#endif
