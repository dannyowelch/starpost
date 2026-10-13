#include "pad.h"

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
    if (pad->b || pad->lt > PAD_TRIGGER)
        io->zap = true;
    if (pad->start)
        io->confirm = true;
}
