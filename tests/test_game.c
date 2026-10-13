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
    CHECK(g.score == (uint32_t)STARPOST_SCORE_KILL);
    CHECK(g.state == STATE_PLAYING);
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
    CHECK(g.score == 0);
    CHECK(g.cfg.wave == 1);
    CHECK(g.spawned == 0);
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

static float spawn_window(int wave)
{
    WaveConfig c = wave_config(wave);
    return (float)c.enemy_count * c.spawn_interval;
}

static void test_wave_scaling(void)
{
    WaveConfig a = wave_config(1);
    WaveConfig b = wave_config(2);
    WaveConfig later = wave_config(100);
    WaveConfig later2 = wave_config(101);

    CHECK(a.wave == 1);
    CHECK(wave_config(0).enemy_count == a.enemy_count);
    CHECK(wave_config(-4).spawn_interval == a.spawn_interval);
    CHECK(a.enemy_count >= 1);
    CHECK(a.enemy_speed > 0.0f);
    CHECK(a.spawn_interval > 0.0f);

    for (int w = 1; w <= 6; w++) {
        WaveConfig cur = wave_config(w);
        WaveConfig next = wave_config(w + 1);
        CHECK(next.enemy_count > cur.enemy_count);
        CHECK(next.enemy_speed > cur.enemy_speed);
        CHECK(next.spawn_interval < cur.spawn_interval);
        CHECK(spawn_window(w + 1) > spawn_window(w));
    }

    CHECK(b.enemy_count > a.enemy_count);
    CHECK(later.enemy_count == later2.enemy_count);
    CHECK(later.enemy_count <= STARPOST_MAX_ENEMIES);
    CHECK(later.spawn_interval == later2.spawn_interval);
    CHECK(later.spawn_interval > 0.0f);
    CHECK(later.enemy_speed == later2.enemy_speed);
    CHECK(later.enemy_speed >= wave_config(8).enemy_speed);
}

static void spawn_wave(GameState *g)
{
    InputFrame none;
    int guard = 0;
    memset(&none, 0, sizeof none);
    while (g->spawned < g->cfg.enemy_count && guard++ < 10000)
        game_update(g, &none, g->cfg.spawn_interval);
}

static void kill_all(GameState *g)
{
    InputFrame in;
    int guard = 0;
    memset(&in, 0, sizeof in);
    in.fire = true;
    while (guard++ < 5000) {
        int lane = -1;
        for (int i = 0; i < STARPOST_MAX_ENEMIES; i++) {
            if (g->enemies[i].alive) {
                lane = g->enemies[i].lane;
                break;
            }
        }
        if (lane < 0)
            break;
        g->player.lane = (uint8_t)lane;
        g->player.cooldown = 0.0f;
        game_update(g, &in, STARPOST_DT);
    }
}

static void test_wave_clear_score_and_transition(void)
{
    GameState g;
    InputFrame none;
    WaveConfig next;
    uint32_t score;
    int count;

    uint8_t lane;

    game_init(&g, 1);
    CHECK(g.state == STATE_PLAYING);
    CHECK(g.cfg.wave == 1);
    spawn_wave(&g);
    CHECK(g.spawned == g.cfg.enemy_count);
    CHECK(g.enemies[0].alive);
    CHECK(g.enemies[0].speed == g.cfg.enemy_speed);
    CHECK(g.enemies[g.spawned - 1].dist == 1.0f);
    CHECK(g.enemies[0].dist < 1.0f);

    g.shield = STARPOST_SHIELD_MAX - 1;
    kill_all(&g);
    count = g.cfg.enemy_count;
    lane = g.player.lane;
    CHECK(g.killed == count);
    CHECK(g.score == (uint32_t)count * (uint32_t)STARPOST_SCORE_KILL +
                         (uint32_t)STARPOST_SCORE_WAVE);
    CHECK(STARPOST_SHIELD_MAX - 1 + STARPOST_SHIELD_RECHARGE > STARPOST_SHIELD_MAX);
    CHECK(g.shield == STARPOST_SHIELD_MAX);
    CHECK(g.state == STATE_WAVE_CLEAR);
    CHECK(g.player.lane == lane);

    score = g.score;
    next = wave_config(2);
    memset(&none, 0, sizeof none);
    game_update(&g, &none, STARPOST_WAVE_CLEAR_TIME * 0.5f);
    CHECK(g.state == STATE_WAVE_CLEAR);
    CHECK(g.cfg.wave == 1);
    game_update(&g, &none, STARPOST_WAVE_CLEAR_TIME);
    CHECK(g.state == STATE_PLAYING);
    CHECK(g.cfg.wave == 2);
    CHECK(g.cfg.enemy_count == next.enemy_count);
    CHECK(g.cfg.enemy_speed == next.enemy_speed);
    CHECK(g.cfg.spawn_interval == next.spawn_interval);
    CHECK(g.spawned == 0);
    CHECK(g.killed == 0);
    CHECK(g.score == score);
    CHECK(g.shield == STARPOST_SHIELD_MAX);
    CHECK(g.player.lane == lane);
    for (int i = 0; i < STARPOST_MAX_ENEMIES; i++)
        CHECK(!g.enemies[i].alive);
}

static void test_recharge_cap_and_arrival(void)
{
    GameState g;
    InputFrame none;
    int n = 0;

    game_init(&g, 3);
    spawn_wave(&g);
    g.shield = 1;
    kill_all(&g);
    CHECK(g.state == STATE_WAVE_CLEAR);
    CHECK(1 + STARPOST_SHIELD_RECHARGE <= STARPOST_SHIELD_MAX);
    CHECK(g.shield == 1 + STARPOST_SHIELD_RECHARGE);

    game_init(&g, 2);
    g.shield = STARPOST_SHIELD_MAX;
    spawn_wave(&g);
    CHECK(g.cfg.enemy_count < STARPOST_SHIELD_MAX);
    for (int i = 0; i < STARPOST_MAX_ENEMIES; i++) {
        if (!g.enemies[i].alive)
            continue;
        g.enemies[i].dist = 0.02f;
        g.enemies[i].speed = 1.0f;
        n += 1;
    }
    CHECK(n == g.cfg.enemy_count);
    memset(&none, 0, sizeof none);
    game_update(&g, &none, 0.05f);
    CHECK(g.state == STATE_WAVE_CLEAR);
    CHECK(g.killed == 0);
    CHECK(g.score == (uint32_t)STARPOST_SCORE_WAVE);
    CHECK(g.shield == STARPOST_SHIELD_MAX - n * STARPOST_SHIELD_DRAIN +
                          STARPOST_SHIELD_RECHARGE);
}

static void test_game_over_skips_wave_clear(void)
{
    GameState g;
    InputFrame in;

    game_init(&g, 1);
    g.spawned = g.cfg.enemy_count;
    g.spawn_timer = 1000.0f;
    g.shield = 1;
    place(&g, 0, 1, 0.01f, 1.0f);
    memset(&in, 0, sizeof in);
    game_update(&g, &in, 0.05f);
    CHECK(g.state == STATE_GAME_OVER);
    CHECK(g.shield == 0);
    CHECK(g.score == 0);
}

int main(void)
{
    test_lane_step_and_wrap();
    test_hit_nearest();
    test_shield_drain();
    test_game_over_and_restart();
    test_recharge_is_partial();
    test_wave_scaling();
    test_wave_clear_score_and_transition();
    test_recharge_cap_and_arrival();
    test_game_over_skips_wave_clear();
    if (fails) {
        fprintf(stderr, "%d/%d checks failed\n", fails, checks);
        return 1;
    }
    printf("%d checks passed\n", checks);
    return 0;
}
