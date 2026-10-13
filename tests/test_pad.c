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

static void test_event_budget(void)
{
    CHECK(pad_event_budget(0) == 0);
    CHECK(pad_event_budget(3) == 3);
    CHECK(pad_event_budget(STARPOST_MAX_PAD_EVENTS) == STARPOST_MAX_PAD_EVENTS);
    CHECK(pad_event_budget(100000) == STARPOST_MAX_PAD_EVENTS);
    CHECK(pad_event_budget(-1) == 0);
}

/* A stick chattering across the deadzone is a held direction, not a
   turn per sample. Two seconds of jitter must stay on the repeat. */
static void test_jitter_does_not_spin(void)
{
    GameState g;
    int lane;
    int turns = 0;
    int i;

    int hold_left = 0;
    int hold_right = 0;

    game_init(&g, 1);
    lane = g.player.lane;
    for (i = 0; i < 240; i++) {
        InputFrame in;
        PadState p = pad_clear();
        float x = (i & 1) ? -0.36f : -0.34f;
        memset(&in, 0, sizeof in);
        pad_stick_hold(x, &hold_left, &hold_right);
        p.stick_x = hold_left ? -1.0f : (hold_right ? 1.0f : 0.0f);
        pad_apply(&in, &p);
        game_update(&g, &in, STARPOST_DT);
        if (g.player.lane != lane) {
            turns++;
            lane = g.player.lane;
        }
    }
    CHECK(turns >= 1);
    CHECK(turns <= 16);
    pad_stick_hold(0.0f, &hold_left, &hold_right);
    CHECK(!hold_left);
    CHECK(!hold_right);
}

/* A burst of axis samples is level state. The frame keeps the latest
   sample, so one update steps at most once. */
static void test_axis_flood_is_level(void)
{
    GameState g;
    InputFrame in;
    PadState p;
    int i;

    game_init(&g, 1);
    memset(&in, 0, sizeof in);
    for (i = 0; i < 10000; i++) {
        memset(&in, 0, sizeof in);
        p = pad_clear();
        p.stick_x = (i & 1) ? 0.9f : -0.9f;
        pad_apply(&in, &p);
    }
    game_update(&g, &in, STARPOST_DT);
    CHECK(g.player.lane == 1);
    CHECK(in.right);
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
    test_event_budget();
    test_jitter_does_not_spin();
    test_axis_flood_is_level();
    if (fails) {
        fprintf(stderr, "%d/%d pad checks failed\n", fails, checks);
        return 1;
    }
    printf("%d pad checks passed\n", checks);
    return 0;
}
