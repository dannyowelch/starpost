#include "game/game.h"

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

static void release(GameState *g)
{
    InputFrame none;
    memset(&none, 0, sizeof none);
    game_update(g, &none, STARPOST_DT);
}

static void test_lane_step_and_wrap(void)
{
    GameState g;
    InputFrame in;

    game_init(&g, 1);
    CHECK(g.player.lane == 0);

    memset(&in, 0, sizeof in);
    in.right = true;
    game_update(&g, &in, STARPOST_DT);
    CHECK(g.player.lane == 1);

    game_update(&g, &in, STARPOST_DT);
    CHECK(g.player.lane == 1);

    game_update(&g, &in, STARPOST_TURN_REPEAT);
    CHECK(g.player.lane == 2);

    release(&g);
    memset(&in, 0, sizeof in);
    in.right = true;
    game_update(&g, &in, STARPOST_DT);
    CHECK(g.player.lane == 3);

    game_init(&g, 1);
    g.player.lane = 7;
    memset(&in, 0, sizeof in);
    in.right = true;
    game_update(&g, &in, STARPOST_DT);
    CHECK(g.player.lane == 0);

    game_init(&g, 1);
    memset(&in, 0, sizeof in);
    in.left = true;
    game_update(&g, &in, STARPOST_DT);
    CHECK(g.player.lane == 7);

    release(&g);
    game_update(&g, &in, STARPOST_DT);
    CHECK(g.player.lane == 6);
}

static void place(GameState *g, int slot, int lane, float dist, float speed)
{
    Enemy *e = &g->enemies[slot];
    e->kind = ENEMY_WALKER;
    e->lane = (uint8_t)lane;
    e->dist = dist;
    e->speed = speed;
    e->alive = true;
}

static void test_hit_nearest(void)
{
    GameState g;
    InputFrame in;

    game_init(&g, 1);
    g.spawn_timer = 1000.0f;
    g.player.lane = 3;
    place(&g, 0, 3, 0.90f, 0.0f);
    place(&g, 1, 3, 0.25f, 0.0f);
    place(&g, 2, 4, 0.05f, 0.0f);

    memset(&in, 0, sizeof in);
    in.fire = true;
    game_update(&g, &in, STARPOST_DT);

    CHECK(g.enemies[0].alive);
    CHECK(!g.enemies[1].alive);
    CHECK(g.enemies[2].alive);
    CHECK(g.player.beam_timer == STARPOST_BEAM_TIME);
    CHECK(g.player.beam_lane == 3);
    CHECK(g.shield == STARPOST_SHIELD_MAX);
}

static void test_shield_drain(void)
{
    GameState g;
    InputFrame in;

    game_init(&g, 1);
    g.spawn_timer = 1000.0f;
    place(&g, 0, 2, 0.01f, 1.0f);
    memset(&in, 0, sizeof in);
    game_update(&g, &in, 0.05f);

    CHECK(!g.enemies[0].alive);
    CHECK(g.shield == STARPOST_SHIELD_MAX - STARPOST_SHIELD_DRAIN);
    CHECK(g.state == STATE_PLAYING);
}

static void test_game_over_and_restart(void)
{
    GameState g;
    InputFrame in;
    int alive;

    game_init(&g, 1);
    g.spawn_timer = 1000.0f;
    g.shield = 1;
    place(&g, 0, 1, 0.01f, 1.0f);
    memset(&in, 0, sizeof in);
    game_update(&g, &in, 0.05f);

    CHECK(g.shield == 0);
    CHECK(g.state == STATE_GAME_OVER);
    CHECK(!g.enemies[0].alive);

    place(&g, 1, 1, 0.01f, 1.0f);
    game_update(&g, &in, 0.05f);
    CHECK(g.shield == 0);
    CHECK(g.state == STATE_GAME_OVER);
    CHECK(g.enemies[1].alive);
    CHECK(g.enemies[1].dist == 0.01f);

    g.player.lane = 3;
    in.fire = true;
    in.right = true;
    game_update(&g, &in, STARPOST_DT);
    CHECK(g.state == STATE_GAME_OVER);
    CHECK(g.player.lane == 3);
    CHECK(g.player.beam_timer == 0.0f);
    CHECK(g.enemies[1].alive);

    memset(&in, 0, sizeof in);
    in.confirm = true;
    game_update(&g, &in, STARPOST_DT);
    CHECK(g.state == STATE_PLAYING);
    CHECK(g.shield == STARPOST_SHIELD_MAX);
    CHECK(g.player.lane == 0);
    CHECK(g.player.beam_timer == 0.0f);
    alive = 0;
    for (int i = 0; i < STARPOST_MAX_ENEMIES; i++)
        alive += g.enemies[i].alive ? 1 : 0;
    CHECK(alive == 0);
}

static void test_recharge_is_partial(void)
{
    CHECK(STARPOST_SHIELD_RECHARGE > 0);
    CHECK(STARPOST_SHIELD_RECHARGE < STARPOST_SHIELD_MAX);
    CHECK(STARPOST_SHIELD_DRAIN > 0);
}

int main(void)
{
    test_lane_step_and_wrap();
    test_hit_nearest();
    test_shield_drain();
    test_game_over_and_restart();
    test_recharge_is_partial();
    if (fails) {
        fprintf(stderr, "%d/%d checks failed\n", fails, checks);
        return 1;
    }
    printf("%d checks passed\n", checks);
    return 0;
}
