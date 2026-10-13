#include "pad.h"

void pad_stick_hold(float stick_x, int *hold_left, int *hold_right)
{
    if (!hold_left || !hold_right)
        return;
    if (stick_x <= -PAD_DEADZONE)
        *hold_left = 1;
    else if (stick_x >= -PAD_RELEASE)
        *hold_left = 0;
    if (stick_x >= PAD_DEADZONE)
        *hold_right = 1;
    else if (stick_x <= PAD_RELEASE)
        *hold_right = 0;
    if (*hold_left && *hold_right) {
        *hold_left = 0;
        *hold_right = 0;
    }
}

int pad_event_budget(int queued)
{
    if (queued < 0)
        return 0;
    if (queued > STARPOST_MAX_PAD_EVENTS)
        return STARPOST_MAX_PAD_EVENTS;
    return queued;
}

void pad_apply(InputFrame *io, const PadState *pad)
{
    int left;
    int right;
    if (!io || !pad)
        return;
    left = pad->dpad_left || pad->stick_x < -PAD_DEADZONE;
    right = pad->dpad_right || pad->stick_x > PAD_DEADZONE;
    if (left && !right)
        io->left = true;
    if (right && !left)
        io->right = true;
    if (pad->a || pad->rt > PAD_TRIGGER)
        io->fire = true;
    if (pad->y || pad->lt > PAD_TRIGGER)
        io->zap = true;
    if (pad->x)
        io->spread = true;
    if (pad->b)
        io->energize = true;
    if (pad->rb)
        io->slow = true;
    if (pad->lb)
        io->repair = true;
    if (pad->start)
        io->confirm = true;
}
