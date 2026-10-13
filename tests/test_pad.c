#include "input/pad.h"

#include <stdio.h>
#include <string.h>

static int fails;
static int checks;

static void check(int cond, const char *expr, const char *file, int line)
{
    checks++;
    if (!cond) {
        fprintf(stderr, "FAIL %s:%d: %s\n", file, line, expr);
        fails++;
    }
}

#define CHECK(cond) check((cond), #cond, __FILE__, __LINE__)

static PadState pad_clear(void)
{
    PadState p;
    memset(&p, 0, sizeof p);
    return p;
}

static void test_deadzone_and_buttons(void)
{
    InputFrame in;
    PadState p = pad_clear();

    memset(&in, 0, sizeof in);
    p.stick_x = 0.35f;
    pad_apply(&in, &p);
    CHECK(!in.left);
    CHECK(!in.right);

    p.stick_x = -0.35f;
    pad_apply(&in, &p);
    CHECK(!in.left);

    memset(&in, 0, sizeof in);
    p.stick_x = -0.36f;
    pad_apply(&in, &p);
    CHECK(in.left);
    CHECK(!in.right);

    memset(&in, 0, sizeof in);
    p = pad_clear();
    p.stick_x = 0.80f;
    pad_apply(&in, &p);
    CHECK(in.right);
    CHECK(!in.left);

    memset(&in, 0, sizeof in);
    p = pad_clear();
    p.stick_x = -0.90f;
    p.dpad_right = 1;
    pad_apply(&in, &p);
    CHECK(!in.left);
    CHECK(!in.right);

    memset(&in, 0, sizeof in);
    p = pad_clear();
    p.dpad_left = 1;
    pad_apply(&in, &p);
    CHECK(in.left);

    memset(&in, 0, sizeof in);
    in.fire = true;
    p = pad_clear();
    pad_apply(&in, &p);
    CHECK(in.fire);

    memset(&in, 0, sizeof in);
    p = pad_clear();
    p.a = 1;
    pad_apply(&in, &p);
    CHECK(in.fire);
    CHECK(!in.zap);

    memset(&in, 0, sizeof in);
    p = pad_clear();
    p.rt = 0.49f;
    pad_apply(&in, &p);
    CHECK(!in.fire);
    p.rt = 0.51f;
    pad_apply(&in, &p);
    CHECK(in.fire);

    memset(&in, 0, sizeof in);
    p = pad_clear();
    p.b = 1;
    pad_apply(&in, &p);
    CHECK(in.zap);
    CHECK(!in.fire);

    memset(&in, 0, sizeof in);
    p = pad_clear();
    p.lt = 0.51f;
    pad_apply(&in, &p);
    CHECK(in.zap);

    memset(&in, 0, sizeof in);
    p = pad_clear();
    p.start = 1;
    pad_apply(&in, &p);
    CHECK(in.confirm);
    CHECK(!in.fire);
    CHECK(!in.left);
}

static void test_stick_repeats_lane(void)
{
    GameState g;
    InputFrame in;
    PadState p = pad_clear();

    game_init(&g, 1);
    memset(&in, 0, sizeof in);
    p.stick_x = -0.20f;
    pad_apply(&in, &p);
    game_update(&g, &in, STARPOST_DT);
    CHECK(g.player.lane == 0);
    CHECK(g.snd_count == 0);

    memset(&in, 0, sizeof in);
    p.stick_x = 0.90f;
    pad_apply(&in, &p);
    game_update(&g, &in, STARPOST_DT);
    CHECK(g.player.lane == 1);
    CHECK(g.snd[0] == GAME_SND_STEP);

    game_update(&g, &in, STARPOST_DT);
    CHECK(g.player.lane == 1);

    game_update(&g, &in, STARPOST_TURN_REPEAT);
    CHECK(g.player.lane == 2);
}

int main(void)
{
    test_deadzone_and_buttons();
    test_stick_repeats_lane();
    if (fails) {
        fprintf(stderr, "%d/%d pad checks failed\n", fails, checks);
        return 1;
    }
    printf("%d pad checks passed\n", checks);
    return 0;
}
