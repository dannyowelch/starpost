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

static float reach(float dist)
{
    return dist / STARPOST_SHOT_SPEED + 0.004f;
}

static int shots_alive(const GameState *g)
{
    int n = 0;
    int i;
    for (i = 0; i < STARPOST_MAX_SHOTS; i++)
        n += g->shots[i].alive ? 1 : 0;
    return n;
}

static void place(GameState *g, int slot, int lane, float dist, float speed)
{
    Enemy *e = &g->enemies[slot];
    e->kind = ENEMY_WALKER;
    e->lane = (uint8_t)lane;
    e->dist = dist;
    e->speed = speed;
    e->angle = 0.0f;
    e->ang_speed = 0.0f;
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
    game_update(&g, &in, reach(0.25f));

    CHECK(g.enemies[0].alive);
    CHECK(!g.enemies[1].alive);
    CHECK(g.enemies[2].alive);
    CHECK(shots_alive(&g) == 0);
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
    CHECK(shots_alive(&g) == 0);
    CHECK(g.enemies[1].alive);

    memset(&in, 0, sizeof in);
    in.confirm = true;
    game_update(&g, &in, STARPOST_DT);
    CHECK(g.state == STATE_PLAYING);
    CHECK(g.shield == STARPOST_SHIELD_MAX);
    CHECK(g.player.lane == 0);
    CHECK(shots_alive(&g) == 0);
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
    CHECK(STARPOST_SHIELD_RECHARGE == 0);
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
    CHECK(a.enemy_speed == 0.15f * 0.85f);
    CHECK(b.enemy_speed == a.enemy_speed + 0.025f * 0.85f);
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
    CHECK(later.enemy_speed == 0.70f);
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
    CHECK(g.shield == STARPOST_SHIELD_MAX - 1);
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
    CHECK(g.shield == STARPOST_SHIELD_MAX - 1);
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
    CHECK(g.shield == 1);

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
    CHECK(g.shield == STARPOST_SHIELD_MAX - n * STARPOST_SHIELD_DRAIN);
}

static void test_flyer_lane_and_spiral(void)
{
    GameState g;
    InputFrame in;
    float start;
    float sector = 6.2831853f / (float)STARPOST_LANES;
    int lane;

    for (lane = 0; lane < STARPOST_LANES; lane++)
        CHECK(starpost_lane_from_angle(starpost_lane_center(lane)) == lane);

    CHECK(starpost_lane_from_angle(starpost_lane_center(0) + sector) == 7);
    CHECK(starpost_lane_from_angle(starpost_lane_center(1) - 0.02f) == 1);

    game_init(&g, 1);
    g.spawn_timer = 1000.0f;
    g.enemies[0].kind = ENEMY_FLYER;
    g.enemies[0].alive = true;
    g.enemies[0].dist = 0.80f;
    g.enemies[0].speed = 0.10f;
    g.enemies[0].ang_speed = STARPOST_FLYER_ANG_SPEED;
    start = starpost_lane_center(0);
    g.enemies[0].angle = start;
    g.enemies[0].lane = (uint8_t)starpost_lane_from_angle(start);
    CHECK(g.enemies[0].lane == 0);

    memset(&in, 0, sizeof in);
    game_update(&g, &in, 0.50f);
    CHECK(g.enemies[0].alive);
    CHECK(g.enemies[0].dist == 0.80f - 0.10f * 0.50f);
    CHECK(g.enemies[0].angle == start + STARPOST_FLYER_ANG_SPEED * 0.50f);
    CHECK(g.enemies[0].lane == (uint8_t)starpost_lane_from_angle(g.enemies[0].angle));
    CHECK(g.enemies[0].lane == 7);
}

static void test_flyer_hit_window_and_damage(void)
{
    GameState g;
    InputFrame in;
    float center = starpost_lane_center(0);
    float sector = 6.2831853f / (float)STARPOST_LANES;
    float half = 0.5f * STARPOST_HIT_WINDOW * sector;
    float outside = center - (half + 0.05f);

    CHECK(STARPOST_HIT_WINDOW > 0.0f);
    CHECK(STARPOST_HIT_WINDOW < 1.0f);
    CHECK(starpost_flyer_in_lane(center, 0));
    CHECK(!starpost_flyer_in_lane(starpost_lane_center(3), 0));
    CHECK(starpost_flyer_in_lane(center - (half - 0.01f), 0));
    CHECK(!starpost_flyer_in_lane(outside, 0));
    CHECK(starpost_lane_from_angle(outside) == 0);

    game_init(&g, 1);
    g.spawn_timer = 1000.0f;
    g.enemies[0].kind = ENEMY_FLYER;
    g.enemies[0].alive = true;
    g.enemies[0].dist = 0.60f;
    g.enemies[0].speed = 0.0f;
    g.enemies[0].ang_speed = 0.0f;
    g.enemies[0].angle = outside;
    g.enemies[0].lane = (uint8_t)starpost_lane_from_angle(outside);
    g.player.lane = 0;
    memset(&in, 0, sizeof in);
    in.fire = true;
    game_update(&g, &in, reach(0.60f));
    CHECK(g.enemies[0].alive);
    CHECK(g.score == 0);
    CHECK(shots_alive(&g) == 1);

    g.enemies[0].angle = center;
    g.enemies[0].lane = 0;
    memset(g.shots, 0, sizeof g.shots);
    g.player.cooldown = 0.0f;
    game_update(&g, &in, reach(0.60f));
    CHECK(!g.enemies[0].alive);
    CHECK(g.score == (uint32_t)STARPOST_SCORE_FLYER * (uint32_t)STARPOST_FLYER_SHOT_MULT);

    game_init(&g, 1);
    g.spawn_timer = 1000.0f;
    g.enemies[0].kind = ENEMY_FLYER;
    g.enemies[0].alive = true;
    g.enemies[0].dist = 0.02f;
    g.enemies[0].speed = 1.0f;
    g.enemies[0].ang_speed = 0.0f;
    g.enemies[0].angle = center;
    CHECK(STARPOST_FLYER_DRAIN > STARPOST_SHIELD_DRAIN);
    memset(&in, 0, sizeof in);
    game_update(&g, &in, 0.05f);
    CHECK(!g.enemies[0].alive);
    CHECK(g.shield == STARPOST_SHIELD_MAX - STARPOST_FLYER_DRAIN);
    CHECK(g.state == STATE_PLAYING);
    CHECK(g.score == 0);
}

static void test_superzap_charges(void)
{
    GameState g;
    InputFrame in;

    game_init(&g, 1);
    CHECK(g.energy == energy_tune()->start);
    CHECK(g.cfg.flyer_count == 0);
    CHECK(wave_config(2).flyer_count >= 1);
    CHECK(wave_config(4).flyer_count > wave_config(2).flyer_count);
    CHECK(wave_config(100).flyer_count == wave_config(101).flyer_count);
    CHECK(wave_config(100).flyer_count <= 4);

    g.state = STATE_WAVE_CLEAR;
    g.clear_timer = 0.01f;
    memset(&in, 0, sizeof in);
    game_update(&g, &in, 0.05f);
    CHECK(g.state == STATE_PLAYING);
    CHECK(g.cfg.wave == 2);
    CHECK(g.energy == energy_tune()->start);

    game_init(&g, 1);
    g.spawn_timer = 1000.0f;
    g.energy = energy_tune()->zap_cost;
    place(&g, 0, 2, 0.50f, 0.0f);
    g.enemies[1].kind = ENEMY_FLYER;
    g.enemies[1].alive = true;
    g.enemies[1].lane = 4;
    g.enemies[1].dist = 0.40f;
    g.enemies[1].speed = 0.0f;
    g.enemies[1].angle = starpost_lane_center(4);
    g.enemies[1].ang_speed = 0.0f;
    memset(&in, 0, sizeof in);
    in.zap = true;
    game_update(&g, &in, STARPOST_DT);
    CHECK(!g.enemies[0].alive);
    CHECK(!g.enemies[1].alive);
    CHECK(g.energy == energy_tune()->walker_energy + energy_tune()->flyer_energy);
    CHECK(g.score == (uint32_t)STARPOST_SCORE_KILL + (uint32_t)STARPOST_SCORE_FLYER);
    CHECK(g.zap_flash == STARPOST_ZAP_FLASH);
    CHECK(g.state == STATE_PLAYING);

    game_update(&g, &in, STARPOST_DT);
    CHECK(g.energy == energy_tune()->walker_energy + energy_tune()->flyer_energy);
    CHECK(g.score == (uint32_t)STARPOST_SCORE_KILL + (uint32_t)STARPOST_SCORE_FLYER);

    game_init(&g, 4);
    g.spawn_timer = 1000.0f;
    g.energy = energy_tune()->zap_cost - 1;
    place(&g, 0, 1, 0.30f, 0.0f);
    g.enemies[1].kind = ENEMY_FLYER;
    g.enemies[1].alive = true;
    g.enemies[1].dist = 0.50f;
    g.enemies[1].speed = 0.0f;
    g.enemies[1].angle = starpost_lane_center(2);
    memset(&in, 0, sizeof in);
    in.zap = true;
    game_update(&g, &in, STARPOST_DT);
    CHECK(g.enemies[0].alive);
    CHECK(g.enemies[1].alive);
    CHECK(g.energy == energy_tune()->zap_cost - 1);
    CHECK(g.score == 0);
    CHECK(g.zap_flash == 0.0f);
    CHECK(g.deny_flash == energy_tune()->deny_time);
}

static int count_snd(const GameState *g, int id)
{
    int n = 0;
    int i;
    for (i = 0; i < g->snd_count; i++) {
        if (g->snd[i] == (uint8_t)id)
            n++;
    }
    return n;
}

static void test_held_fire_interval(void)
{
    GameState g;
    InputFrame in;

    CHECK(STARPOST_FIRE_COOLDOWN == 0.16f);

    game_init(&g, 1);
    g.spawn_timer = 1000.0f;
    place(&g, 0, 0, 0.30f, 0.0f);
    place(&g, 1, 0, 0.55f, 0.0f);
    place(&g, 2, 0, 0.80f, 0.0f);
    memset(&in, 0, sizeof in);
    in.fire = true;

    game_update(&g, &in, reach(0.30f));
    CHECK(!g.enemies[0].alive);
    CHECK(g.enemies[1].alive);
    CHECK(g.player.cooldown == STARPOST_FIRE_COOLDOWN);
    CHECK(count_snd(&g, GAME_SND_FIRE) == 1);

    game_update(&g, &in, STARPOST_FIRE_COOLDOWN * 0.5f);
    CHECK(g.enemies[1].alive);
    CHECK(count_snd(&g, GAME_SND_FIRE) == 0);
    CHECK(g.player.cooldown > 0.0f);

    game_init(&g, 1);
    g.spawn_timer = 1000.0f;
    place(&g, 0, 0, 0.30f, 0.0f);
    place(&g, 1, 0, 0.55f, 0.0f);
    memset(&in, 0, sizeof in);
    in.fire = true;
    game_update(&g, &in, reach(0.30f));
    CHECK(!g.enemies[0].alive);
    CHECK(g.enemies[1].alive);
    game_update(&g, &in, STARPOST_FIRE_COOLDOWN);
    CHECK(g.enemies[1].alive);
    CHECK(count_snd(&g, GAME_SND_FIRE) == 1);
    CHECK(g.player.cooldown == STARPOST_FIRE_COOLDOWN);
    CHECK(shots_alive(&g) == 1);
    in.fire = false;
    g.player.cooldown = 10.0f;
    game_update(&g, &in, reach(0.55f));
    CHECK(!g.enemies[1].alive);
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

static void place_enemy(GameState *g, int slot, EnemyKind kind, int lane, float dist)
{
    Enemy *e = &g->enemies[slot];
    memset(e, 0, sizeof *e);
    e->kind = kind;
    e->alive = true;
    e->lane = (uint8_t)lane;
    e->dist = dist;
    e->speed = 0.0f;
    e->angle = starpost_lane_center(lane);
    e->ang_speed = 0.0f;
}

static float gauge_after(int quota, int resolved)
{
    return (float)(quota - resolved) / (float)quota;
}

static void test_wave_gauge(void)
{
    GameState g;
    InputFrame in;
    int quota;
    int i;
    int energy_before;

    game_init(&g, 1);
    CHECK(starpost_wave_gauge(NULL) == 0.0f);
    CHECK(starpost_wave_gauge(&g) == 1.0f);
    CHECK(g.killed == 0);
    CHECK(g.arrived == 0);

    g.state = STATE_WAVE_CLEAR;
    g.clear_timer = 0.01f;
    memset(&in, 0, sizeof in);
    game_update(&g, &in, 0.05f);
    CHECK(g.cfg.wave == 2);
    CHECK(g.cfg.flyer_count >= 1);
    quota = g.cfg.enemy_count + g.cfg.flyer_count;
    CHECK(quota > g.cfg.enemy_count);
    CHECK(starpost_wave_gauge(&g) == 1.0f);
    g.energy = energy_tune()->zap_cost;
    energy_before = g.energy;

    g.spawn_timer = 1000.0f;
    place_enemy(&g, 0, ENEMY_WALKER, 0, 0.55f);
    g.player.lane = 0;
    g.player.cooldown = 0.0f;
    memset(&in, 0, sizeof in);
    in.fire = true;
    game_update(&g, &in, reach(0.55f));
    CHECK(g.killed == 1);
    CHECK(g.arrived == 0);
    CHECK(starpost_wave_gauge(&g) == gauge_after(quota, 1));

    place_enemy(&g, 1, ENEMY_FLYER, 3, 0.02f);
    g.enemies[1].speed = 1.0f;
    g.player.cooldown = 10.0f;
    memset(&in, 0, sizeof in);
    game_update(&g, &in, 0.05f);
    CHECK(!g.enemies[1].alive);
    CHECK(g.arrived == 1);
    CHECK(g.killed == 1);
    CHECK(g.shield == STARPOST_SHIELD_MAX - STARPOST_FLYER_DRAIN);
    CHECK(starpost_wave_gauge(&g) == gauge_after(quota, 2));

    place_enemy(&g, 2, ENEMY_WALKER, 1, 0.40f);
    place_enemy(&g, 3, ENEMY_WALKER, 2, 0.40f);
    place_enemy(&g, 4, ENEMY_FLYER, 4, 0.40f);
    memset(&in, 0, sizeof in);
    in.zap = true;
    game_update(&g, &in, STARPOST_DT);
    CHECK(g.killed == 4);
    CHECK(g.arrived == 1);
    CHECK(!g.enemies[2].alive);
    CHECK(!g.enemies[3].alive);
    CHECK(!g.enemies[4].alive);
    CHECK(g.energy == energy_before - energy_tune()->zap_cost + 2 * energy_tune()->walker_energy +
                             energy_tune()->flyer_energy);
    CHECK(starpost_wave_gauge(&g) == gauge_after(quota, 5));

    for (i = 0; i < quota - 5; i++)
        place_enemy(&g, 5 + i, ENEMY_WALKER, i % STARPOST_LANES, 0.50f);
    g.spawned = quota;
    g.spawn_timer = 1000.0f;
    kill_all(&g);
    CHECK(g.state == STATE_WAVE_CLEAR);
    CHECK(g.killed + g.arrived == quota);
    CHECK(starpost_wave_gauge(&g) == 0.0f);

    memset(&in, 0, sizeof in);
    game_update(&g, &in, STARPOST_WAVE_CLEAR_TIME);
    CHECK(g.state == STATE_PLAYING);
    CHECK(g.cfg.wave == 3);
    CHECK(g.killed == 0);
    CHECK(g.arrived == 0);
    CHECK(starpost_wave_gauge(&g) == 1.0f);
}

static void test_energy_abilities(void)
{
    const EnergyTune *t = energy_tune();
    GameState g;
    InputFrame in;
    float walker_dist;
    float flyer_dist;
    int found = 0;
    uint32_t seed;

    CHECK(t->start == 20);
    CHECK(t->cap == 100);
    CHECK(t->wave_grant == 10);
    CHECK(t->shot_cost == 1);
    CHECK(t->walker_energy == 1);
    CHECK(t->flyer_energy == 5);
    CHECK(t->free_shot_at_zero == 0);
    CHECK(t->zap_cost == 20);
    CHECK(t->spread_cost == 2);
    CHECK(t->energize_cost == 5);
    CHECK(t->energize_time == 3.0f);
    CHECK(t->slow_cost == 10);
    CHECK(t->repair_cost == 10);
    CHECK(t->slow_time == 5.0f);
    CHECK(t->slow_factor == 0.5f);

    game_init(&g, 1);
    CHECK(g.energy == 20);
    g.energy = t->cap - 5;
    g.spawned = g.cfg.enemy_count + g.cfg.flyer_count;
    g.spawn_timer = 1000.0f;
    memset(&in, 0, sizeof in);
    game_update(&g, &in, STARPOST_DT);
    CHECK(g.state == STATE_WAVE_CLEAR);
    CHECK(g.energy == t->cap);

    game_init(&g, 1);
    g.spawn_timer = 1000.0f;
    g.player.lane = 0;
    place(&g, 0, 7, 0.80f, 0.0f);
    place(&g, 1, 7, 0.30f, 0.0f);
    place(&g, 2, 0, 0.50f, 0.0f);
    place(&g, 3, 1, 0.40f, 0.0f);
    place(&g, 4, 3, 0.20f, 0.0f);
    g.enemies[5].kind = ENEMY_FLYER;
    g.enemies[5].alive = true;
    g.enemies[5].dist = 0.60f;
    g.enemies[5].speed = 0.0f;
    g.enemies[5].angle = starpost_lane_center(0);
    g.enemies[5].ang_speed = 0.0f;
    g.enemies[6].kind = ENEMY_FLYER;
    g.enemies[6].alive = true;
    g.enemies[6].dist = 0.60f;
    g.enemies[6].speed = 0.0f;
    g.enemies[6].angle = starpost_lane_center(4);
    g.enemies[6].ang_speed = 0.0f;
    g.energy = 40;
    memset(&in, 0, sizeof in);
    in.spread = true;
    game_update(&g, &in, reach(0.50f));
    CHECK(g.energy == 40 - t->spread_cost + 3 * t->walker_energy);
    CHECK(g.enemies[0].alive);
    CHECK(!g.enemies[1].alive);
    CHECK(!g.enemies[2].alive);
    CHECK(!g.enemies[3].alive);
    CHECK(g.enemies[4].alive);
    CHECK(g.enemies[5].alive);
    CHECK(g.enemies[6].alive);
    game_update(&g, &in, STARPOST_DT);
    CHECK(g.energy == 40 - t->spread_cost + 3 * t->walker_energy);

    g.energy = 0;
    memset(&in, 0, sizeof in);
    game_update(&g, &in, STARPOST_DT);
    in.spread = true;
    game_update(&g, &in, STARPOST_DT);
    CHECK(g.energy == 0);
    CHECK(g.deny_flash == t->deny_time);
    CHECK(g.enemies[4].alive);

    game_init(&g, 1);
    g.spawn_timer = 1000.0f;
    g.player.lane = 2;
    place(&g, 0, 2, 0.70f, 0.0f);
    place(&g, 1, 2, 0.30f, 0.0f);
    place(&g, 2, 5, 0.40f, 0.0f);
    g.enemies[3].kind = ENEMY_FLYER;
    g.enemies[3].alive = true;
    g.enemies[3].dist = 0.50f;
    g.enemies[3].speed = 0.0f;
    g.enemies[3].ang_speed = 0.0f;
    g.enemies[3].angle = starpost_lane_center(2);
    g.enemies[4].kind = ENEMY_FLYER;
    g.enemies[4].alive = true;
    g.enemies[4].dist = 0.50f;
    g.enemies[4].speed = 0.0f;
    g.enemies[4].ang_speed = 0.0f;
    g.enemies[4].angle = starpost_lane_center(6);
    g.energy = 40;
    memset(&in, 0, sizeof in);
    in.energize = true;
    game_update(&g, &in, STARPOST_DT);
    CHECK(g.energy == 40 - t->energize_cost + 2 * t->walker_energy + t->flyer_energy);
    CHECK(g.energize_left[2] == t->energize_time);
    CHECK(g.energize_left[0] == 0.0f);
    CHECK(!g.enemies[0].alive);
    CHECK(!g.enemies[1].alive);
    CHECK(g.enemies[2].alive);
    CHECK(!g.enemies[3].alive);
    CHECK(g.enemies[4].alive);

    place(&g, 5, 2, 0.90f, 0.0f);
    memset(&in, 0, sizeof in);
    game_update(&g, &in, STARPOST_DT);
    CHECK(!g.enemies[5].alive);
    CHECK(g.enemies[2].alive);

    g.enemies[6].kind = ENEMY_FLYER;
    g.enemies[6].alive = true;
    g.enemies[6].dist = 0.80f;
    g.enemies[6].speed = 0.0f;
    g.enemies[6].angle = starpost_lane_center(2) - 0.50f;
    g.enemies[6].ang_speed = 0.50f / STARPOST_DT;
    game_update(&g, &in, STARPOST_DT);
    CHECK(!g.enemies[6].alive);
    CHECK(g.enemies[4].alive);

    g.energize_left[2] = STARPOST_DT * 0.5f;
    place(&g, 7, 2, 0.50f, 0.0f);
    game_update(&g, &in, STARPOST_DT);
    CHECK(g.energize_left[2] == 0.0f);
    CHECK(g.enemies[7].alive);

    for (seed = 1; seed < 400 && !found; seed++) {
        GameState s;
        InputFrame idle;
        int i;
        game_init(&s, seed);
        s.spawn_timer = 0.0f;
        s.energize_left[3] = t->energize_time;
        s.player.cooldown = 10.0f;
        memset(&idle, 0, sizeof idle);
        game_update(&s, &idle, 0.001f);
        for (i = 0; i < STARPOST_MAX_ENEMIES; i++) {
            if (s.enemies[i].kind == ENEMY_WALKER && s.enemies[i].lane == 3 &&
                s.enemies[i].dist > 0.9f) {
                CHECK(!s.enemies[i].alive);
                found = 1;
            }
        }
    }
    CHECK(found);

    game_init(&g, 1);
    g.spawn_timer = 1000.0f;
    place(&g, 0, 1, 0.80f, 0.40f);
    g.enemies[1].kind = ENEMY_FLYER;
    g.enemies[1].alive = true;
    g.enemies[1].lane = 3;
    g.enemies[1].dist = 0.80f;
    g.enemies[1].speed = 0.40f;
    g.enemies[1].angle = starpost_lane_center(3);
    g.enemies[1].ang_speed = 1.0f;
    g.energy = 40;
    memset(&in, 0, sizeof in);
    in.slow = true;
    game_update(&g, &in, 0.50f);
    CHECK(g.energy == 40 - t->slow_cost);
    CHECK(g.slow_timer > 0.0f);
    walker_dist = g.enemies[0].dist;
    flyer_dist = g.enemies[1].dist;
    CHECK(walker_dist == 0.80f - 0.40f * t->slow_factor * 0.50f);
    CHECK(flyer_dist == 0.80f - 0.40f * 0.50f);

    g.slow_timer = 0.10f;
    g.enemies[0].dist = 0.80f;
    g.enemies[0].alive = true;
    memset(&in, 0, sizeof in);
    game_update(&g, &in, 0.10f);
    CHECK(g.slow_timer == 0.0f);
    CHECK(g.enemies[0].dist == 0.80f - 0.40f * 0.10f);

    g.energy = t->slow_cost - 1;
    memset(&in, 0, sizeof in);
    game_update(&g, &in, STARPOST_DT);
    in.slow = true;
    game_update(&g, &in, STARPOST_DT);
    CHECK(g.slow_timer == 0.0f);
    CHECK(g.deny_flash == t->deny_time);

    game_init(&g, 1);
    g.spawn_timer = 1000.0f;
    g.player.lane = 0;
    memset(&in, 0, sizeof in);
    in.fire = true;
    game_update(&g, &in, STARPOST_DT);
    CHECK(g.energy == t->start - t->shot_cost);
    CHECK(shots_alive(&g) == 1);
    CHECK(count_snd(&g, GAME_SND_FIRE) == 1);

    place(&g, 0, 0, 0.40f, 0.0f);
    memset(g.shots, 0, sizeof g.shots);
    g.player.cooldown = 0.0f;
    game_update(&g, &in, reach(0.40f));
    CHECK(!g.enemies[0].alive);
    CHECK(g.energy == t->start - 2 * t->shot_cost + t->walker_energy);

    g.player.cooldown = 0.0f;
    memset(g.shots, 0, sizeof g.shots);
    g.enemies[1].kind = ENEMY_FLYER;
    g.enemies[1].alive = true;
    g.enemies[1].dist = 0.50f;
    g.enemies[1].speed = 0.0f;
    g.enemies[1].ang_speed = 0.0f;
    g.enemies[1].angle = starpost_lane_center(0);
    game_update(&g, &in, reach(0.50f));
    CHECK(!g.enemies[1].alive);
    CHECK(g.energy == t->start - 3 * t->shot_cost + t->walker_energy +
                         STARPOST_FLYER_SHOT_MULT * t->flyer_energy);

    game_init(&g, 1);
    g.spawn_timer = 1000.0f;
    g.energy = 0;
    g.player.lane = 0;
    place(&g, 2, 0, 0.30f, 0.0f);
    memset(&in, 0, sizeof in);
    in.fire = true;
    game_update(&g, &in, STARPOST_DT);
    CHECK(g.enemies[2].alive);
    CHECK(g.energy == 0);
    CHECK(shots_alive(&g) == 0);
    CHECK(g.deny_flash == t->deny_time);
    CHECK(count_snd(&g, GAME_SND_DENY) == 1);
    game_update(&g, &in, STARPOST_FIRE_COOLDOWN * 0.5f);
    CHECK(count_snd(&g, GAME_SND_DENY) == 0);
    game_update(&g, &in, STARPOST_FIRE_COOLDOWN);
    CHECK(count_snd(&g, GAME_SND_DENY) == 1);

    {
        EnergyTune allow = *t;
        allow.free_shot_at_zero = 1;
        CHECK(!starpost_normal_shot_allowed(t, 0));
        CHECK(starpost_normal_shot_allowed(&allow, 0));
        CHECK(starpost_normal_shot_allowed(t, t->shot_cost));
        CHECK(!starpost_normal_shot_allowed(t, t->shot_cost - 1));
    }

    g.energy = t->start;
    g.spawn_timer = 1000.0f;
    place(&g, 3, 1, 0.01f, 1.0f);
    memset(&in, 0, sizeof in);
    game_update(&g, &in, 0.05f);
    CHECK(!g.enemies[3].alive);
    CHECK(g.energy == t->start);

    game_init(&g, 1);
    g.spawn_timer = 1000.0f;
    g.energy = t->cap;
    g.player.lane = 0;
    place(&g, 4, 0, 0.40f, 0.0f);
    memset(&in, 0, sizeof in);
    in.fire = true;
    game_update(&g, &in, reach(0.40f));
    CHECK(!g.enemies[4].alive);
    CHECK(g.energy == t->cap);

    game_init(&g, 1);
    g.energy = 0;
    g.spawned = g.cfg.enemy_count + g.cfg.flyer_count;
    g.spawn_timer = 1000.0f;
    memset(&in, 0, sizeof in);
    game_update(&g, &in, STARPOST_DT);
    CHECK(g.state == STATE_WAVE_CLEAR);
    CHECK(g.energy == t->wave_grant);

    game_init(&g, 1);
    g.spawn_timer = 1000.0f;
    g.shield = 3;
    g.energy = 40;
    memset(&in, 0, sizeof in);
    in.repair = true;
    game_update(&g, &in, STARPOST_DT);
    CHECK(g.shield == 4);
    CHECK(g.energy == 40 - t->repair_cost);
    CHECK(g.repair_flash == t->repair_glow);
    CHECK(count_snd(&g, GAME_SND_REPAIR) == 1);
    game_update(&g, &in, STARPOST_DT);
    CHECK(g.shield == 4);
    CHECK(g.energy == 40 - t->repair_cost);
    memset(&in, 0, sizeof in);
    game_update(&g, &in, STARPOST_DT);
    in.repair = true;
    game_update(&g, &in, STARPOST_DT);
    CHECK(g.shield == 5);
    CHECK(g.energy == 40 - 2 * t->repair_cost);

    g.energy = t->repair_cost - 1;
    memset(&in, 0, sizeof in);
    game_update(&g, &in, STARPOST_DT);
    in.repair = true;
    game_update(&g, &in, STARPOST_DT);
    CHECK(g.shield == 5);
    CHECK(g.energy == t->repair_cost - 1);
    CHECK(g.deny_flash == t->deny_time);

    g.shield = g.shield_max;
    g.energy = 40;
    memset(&in, 0, sizeof in);
    game_update(&g, &in, STARPOST_DT);
    in.repair = true;
    game_update(&g, &in, STARPOST_DT);
    CHECK(g.shield == g.shield_max);
    CHECK(g.energy == 40);
    CHECK(g.deny_flash == t->deny_time);

    g.shield = g.shield_max - 1;
    g.energy = 40;
    g.state = STATE_WAVE_CLEAR;
    g.clear_timer = 10.0f;
    memset(&in, 0, sizeof in);
    game_update(&g, &in, STARPOST_DT);
    in.repair = true;
    game_update(&g, &in, STARPOST_DT);
    CHECK(g.state == STATE_WAVE_CLEAR);
    CHECK(g.shield == g.shield_max);
    CHECK(g.energy == 40 - t->repair_cost);
}

static int alive_kind_lane(const GameState *g, EnemyKind kind, int lane)
{
    int n = 0;
    int i;
    for (i = 0; i < STARPOST_MAX_ENEMIES; i++) {
        const Enemy *e = &g->enemies[i];
        if (e->alive && e->kind == kind && (int)e->lane == lane)
            n++;
    }
    return n;
}

static void test_splitter(void)
{
    const EnergyTune *t = energy_tune();
    GameState g;
    InputFrame in;
    int energy;
    int quota;
    float gauge;
    float dist;
    int guard;
    int found;

    CHECK(t->splitter_energy == 1);
    CHECK(t->split_energy == 1);
    CHECK(STARPOST_SPLITTER_SPEED_SCALE < 1.0f);
    CHECK(STARPOST_SPLIT_SPEED_SCALE > 1.0f);
    CHECK(STARPOST_SPLITTER_DRAIN == 2);
    CHECK(STARPOST_SPLIT_DRAIN == 1);
    CHECK(STARPOST_SCORE_SPLITTER == 200);
    CHECK(STARPOST_SCORE_SPLIT == 50);
    CHECK(wave_config(1).splitter_count == 0);
    CHECK(wave_config(2).splitter_count == 0);
    CHECK(wave_config(3).splitter_count == 1);
    CHECK(wave_config(6).splitter_count == 2);
    CHECK(wave_config(9).splitter_count == 3);
    CHECK(wave_config(100).splitter_count == 3);
    CHECK(wave_config_diff(3, DIFF_ADVANCED).splitter_count == wave_config(3).splitter_count);
    CHECK(wave_config_diff(4, DIFF_BEGINNER).splitter_count == 0);
    CHECK(wave_config_diff(5, DIFF_BEGINNER).splitter_count >= 1);
    CHECK(wave_config_diff(1, DIFF_HYPER).splitter_count == 0);
    CHECK(wave_config_diff(2, DIFF_HYPER).splitter_count >= 1);
    CHECK(diff_tune(DIFF_BEGINNER)->splitter_start == 5);
    CHECK(diff_tune(DIFF_ADVANCED)->splitter_start == 3);
    CHECK(diff_tune(DIFF_HYPER)->splitter_start == 2);

    game_init(&g, 1);
    g.spawn_timer = 1000.0f;
    g.cfg.enemy_count = 0;
    g.cfg.flyer_count = 0;
    g.cfg.splitter_count = 1;
    quota = 3;
    CHECK(starpost_wave_gauge(&g) == 1.0f);
    g.player.lane = 0;
    place_enemy(&g, 0, ENEMY_SPLITTER, 0, 0.55f);
    energy = g.energy;
    gauge = starpost_wave_gauge(&g);
    memset(&in, 0, sizeof in);
    in.fire = true;
    game_update(&g, &in, reach(0.55f));
    CHECK(alive_kind_lane(&g, ENEMY_SPLITTER, 0) == 0);
    CHECK(alive_kind_lane(&g, ENEMY_SPLIT, 7) == 1);
    CHECK(alive_kind_lane(&g, ENEMY_SPLIT, 1) == 1);
    CHECK(g.energy == energy - t->shot_cost + t->splitter_energy);
    CHECK(g.score == (uint32_t)STARPOST_SCORE_SPLITTER);
    CHECK(count_snd(&g, GAME_SND_SPLIT) == 1);
    CHECK(count_snd(&g, GAME_SND_SPLIT_KILL) == 0);
    CHECK(g.quota_skip == 0);
    CHECK(starpost_wave_gauge(&g) < gauge);
    CHECK(starpost_wave_gauge(&g) == gauge_after(quota, 1));
    {
        int i;
        for (i = 0; i < STARPOST_MAX_ENEMIES; i++) {
            const Enemy *e = &g.enemies[i];
            if (!e->alive || e->kind != ENEMY_SPLIT)
                continue;
            CHECK(e->speed == g.cfg.enemy_speed * STARPOST_SPLIT_SPEED_SCALE);
            CHECK(e->speed > g.cfg.enemy_speed);
            CHECK(e->dist < 0.55f);
            CHECK(e->dist > 0.50f);
        }
    }

    place_enemy(&g, 2, ENEMY_SPLITTER, 7, 0.40f);
    g.player.lane = 7;
    g.player.cooldown = 0.0f;
    energy = g.energy;
    gauge = starpost_wave_gauge(&g);
    memset(&in, 0, sizeof in);
    in.fire = true;
    game_update(&g, &in, reach(0.40f));
    CHECK(alive_kind_lane(&g, ENEMY_SPLIT, 6) == 1);
    CHECK(alive_kind_lane(&g, ENEMY_SPLIT, 0) == 1);
    CHECK(starpost_wave_gauge(&g) < gauge);

    g.player.lane = 1;
    g.player.cooldown = 0.0f;
    energy = g.energy;
    memset(&in, 0, sizeof in);
    in.fire = true;
    game_update(&g, &in, reach(0.60f));
    CHECK(alive_kind_lane(&g, ENEMY_SPLIT, 1) == 0);
    CHECK(g.energy == energy - t->shot_cost + t->split_energy);
    CHECK(count_snd(&g, GAME_SND_SPLIT_KILL) == 1);
    CHECK(g.score == (uint32_t)STARPOST_SCORE_SPLITTER * 2u + (uint32_t)STARPOST_SCORE_SPLIT);

    game_init(&g, 1);
    g.spawn_timer = 1000.0f;
    g.cfg.enemy_count = 0;
    g.cfg.flyer_count = 0;
    g.cfg.splitter_count = 1;
    g.player.lane = 0;
    place_enemy(&g, 0, ENEMY_SPLITTER, 0, 0.50f);
    g.energy = t->zap_cost;
    memset(&in, 0, sizeof in);
    in.zap = true;
    game_update(&g, &in, STARPOST_DT);
    CHECK(g.state == STATE_PLAYING);
    CHECK(!g.enemies[0].alive);
    CHECK(alive_kind_lane(&g, ENEMY_SPLIT, 7) == 0);
    CHECK(alive_kind_lane(&g, ENEMY_SPLIT, 1) == 0);
    CHECK(g.quota_skip == 2);
    CHECK(g.killed == 1);
    CHECK(g.energy == t->splitter_energy);
    CHECK(g.score == (uint32_t)STARPOST_SCORE_SPLITTER);
    CHECK(count_snd(&g, GAME_SND_SPLIT) == 0);
    CHECK(starpost_wave_gauge(&g) == 0.0f);

    game_init(&g, 1);
    g.spawn_timer = 1000.0f;
    g.player.lane = 0;
    g.energy = 40;
    memset(&in, 0, sizeof in);
    in.energize = true;
    game_update(&g, &in, STARPOST_DT);
    place_enemy(&g, 0, ENEMY_SPLITTER, 1, 0.50f);
    g.player.lane = 1;
    g.player.cooldown = 0.0f;
    energy = g.energy;
    memset(&in, 0, sizeof in);
    in.fire = true;
    game_update(&g, &in, reach(0.50f));
    CHECK(alive_kind_lane(&g, ENEMY_SPLITTER, 1) == 0);
    CHECK(alive_kind_lane(&g, ENEMY_SPLIT, 0) == 0);
    CHECK(alive_kind_lane(&g, ENEMY_SPLIT, 2) == 1);
    CHECK(g.energy == energy - t->shot_cost + t->splitter_energy + t->split_energy);
    CHECK(count_snd(&g, GAME_SND_SPLIT) == 1);
    CHECK(count_snd(&g, GAME_SND_SPLIT_KILL) == 1);

    game_init(&g, 1);
    g.spawn_timer = 1000.0f;
    g.player.lane = 0;
    place_enemy(&g, 0, ENEMY_SPLITTER, 0, 0.50f);
    energy = g.energy;
    memset(&in, 0, sizeof in);
    in.spread = true;
    game_update(&g, &in, reach(0.50f));
    CHECK(alive_kind_lane(&g, ENEMY_SPLIT, 7) == 1);
    CHECK(alive_kind_lane(&g, ENEMY_SPLIT, 1) == 1);
    CHECK(g.energy == energy - t->spread_cost + t->splitter_energy);
    CHECK(count_snd(&g, GAME_SND_SPLIT_KILL) == 0);

    game_init(&g, 1);
    g.spawn_timer = 1000.0f;
    g.cfg.enemy_count = 0;
    g.cfg.flyer_count = 0;
    g.cfg.splitter_count = 1;
    place_enemy(&g, 0, ENEMY_SPLITTER, 3, 0.02f);
    g.enemies[0].speed = 1.0f;
    memset(&in, 0, sizeof in);
    game_update(&g, &in, 0.05f);
    CHECK(!g.enemies[0].alive);
    CHECK(g.shield == STARPOST_SHIELD_MAX - STARPOST_SPLITTER_DRAIN);
    CHECK(g.arrived == 1);
    CHECK(g.quota_skip == 2);
    CHECK(g.killed == 0);
    CHECK(g.score == 0);
    CHECK(alive_kind_lane(&g, ENEMY_SPLIT, 2) == 0);
    CHECK(alive_kind_lane(&g, ENEMY_SPLIT, 4) == 0);
    CHECK(starpost_wave_gauge(&g) == 0.0f);

    place_enemy(&g, 1, ENEMY_SPLIT, 2, 0.02f);
    g.enemies[1].speed = 1.0f;
    game_update(&g, &in, 0.05f);
    CHECK(g.shield == STARPOST_SHIELD_MAX - STARPOST_SPLITTER_DRAIN - STARPOST_SPLIT_DRAIN);
    CHECK(g.quota_skip == 2);
    CHECK(g.arrived == 2);

    game_init(&g, 1);
    g.spawn_timer = 1000.0f;
    place_enemy(&g, 0, ENEMY_SPLITTER, 1, 0.80f);
    g.enemies[0].speed = 0.40f;
    place_enemy(&g, 1, ENEMY_FLYER, 3, 0.80f);
    g.enemies[1].speed = 0.40f;
    g.enemies[1].ang_speed = 0.0f;
    memset(&in, 0, sizeof in);
    in.slow = true;
    game_update(&g, &in, 0.50f);
    CHECK(g.enemies[0].alive);
    CHECK(g.enemies[0].dist == 0.80f - 0.40f * t->slow_factor * 0.50f);
    CHECK(g.enemies[1].dist == 0.80f - 0.40f * 0.50f);

    game_init(&g, 1);
    g.state = STATE_WAVE_CLEAR;
    g.clear_timer = 0.01f;
    memset(&in, 0, sizeof in);
    game_update(&g, &in, 0.05f);
    g.state = STATE_WAVE_CLEAR;
    g.clear_timer = 0.01f;
    game_update(&g, &in, 0.05f);
    CHECK(g.cfg.wave == 3);
    CHECK(g.cfg.splitter_count >= 1);
    found = 0;
    guard = 0;
    while (!found && guard++ < 4000) {
        int i;
        game_update(&g, &in, g.cfg.spawn_interval);
        for (i = 0; i < STARPOST_MAX_ENEMIES; i++) {
            if (g.enemies[i].alive && g.enemies[i].kind == ENEMY_SPLITTER) {
                CHECK(g.enemies[i].speed == g.cfg.enemy_speed * STARPOST_SPLITTER_SPEED_SCALE);
                CHECK(g.enemies[i].speed < g.cfg.enemy_speed);
                found = 1;
            }
        }
    }
    CHECK(found == 1);

    dist = starpost_wave_gauge(&g);
    CHECK(dist <= 1.0f);
    CHECK(dist >= 0.0f);
}

static int shot_in_lane(const GameState *g, int lane, int spread)
{
    int n = 0;
    int i;
    for (i = 0; i < STARPOST_MAX_SHOTS; i++) {
        const Shot *s = &g->shots[i];
        if (!s->alive || (int)s->lane != lane)
            continue;
        if ((int)s->spread != spread)
            continue;
        n++;
    }
    return n;
}

static void test_projectiles_and_lanes(void)
{
    const EnergyTune *t = energy_tune();
    GameState g;
    InputFrame in;
    float center = starpost_lane_center(0);
    float sector = 6.2831853f / (float)STARPOST_LANES;
    float half = 0.5f * STARPOST_HIT_WINDOW * sector;
    float meet = 0.40f / STARPOST_SHOT_SPEED;
    float ang_speed;
    int energy;

    CHECK(STARPOST_SHOT_SPEED == 2.5f);
    CHECK(STARPOST_FLYER_SHOT_MULT == 3);
    CHECK(STARPOST_FIRE_COOLDOWN == 0.16f);

    game_init(&g, 1);
    g.spawn_timer = 1000.0f;
    g.player.lane = 0;
    place(&g, 0, 0, 0.30f, 0.0f);
    place(&g, 1, 0, 0.70f, 0.0f);
    memset(&in, 0, sizeof in);
    in.fire = true;
    game_update(&g, &in, 0.05f);
    CHECK(g.enemies[0].alive);
    CHECK(g.enemies[1].alive);
    CHECK(shots_alive(&g) == 1);
    g.player.cooldown = 0.0f;
    game_update(&g, &in, 0.05f);
    CHECK(shots_alive(&g) == 2);
    CHECK(g.enemies[0].alive);
    CHECK(g.enemies[1].alive);
    in.fire = false;
    g.player.cooldown = 10.0f;
    game_update(&g, &in, (0.70f - 0.125f) / STARPOST_SHOT_SPEED + 0.004f);
    CHECK(!g.enemies[0].alive);
    CHECK(!g.enemies[1].alive);
    CHECK(g.score == 2u * (uint32_t)STARPOST_SCORE_KILL);

    game_init(&g, 1);
    g.spawn_timer = 1000.0f;
    g.player.lane = 0;
    energy = g.energy;
    memset(&in, 0, sizeof in);
    in.fire = true;
    game_update(&g, &in, 1.0f / STARPOST_SHOT_SPEED + 0.01f);
    CHECK(shots_alive(&g) == 0);
    CHECK(g.score == 0);
    CHECK(g.energy == energy - t->shot_cost);

    game_init(&g, 1);
    g.spawn_timer = 1000.0f;
    g.player.lane = 0;
    place(&g, 0, 7, 0.60f, 0.0f);
    place(&g, 1, 0, 0.60f, 0.0f);
    place(&g, 2, 1, 0.60f, 0.0f);
    place(&g, 3, 2, 0.40f, 0.0f);
    memset(&in, 0, sizeof in);
    in.spread = true;
    game_update(&g, &in, STARPOST_DT);
    CHECK(shot_in_lane(&g, 7, 1) == 1);
    CHECK(shot_in_lane(&g, 0, 1) == 1);
    CHECK(shot_in_lane(&g, 1, 1) == 1);
    CHECK(shots_alive(&g) == 3);
    in.spread = false;
    game_update(&g, &in, reach(0.60f));
    CHECK(!g.enemies[0].alive);
    CHECK(!g.enemies[1].alive);
    CHECK(!g.enemies[2].alive);
    CHECK(g.enemies[3].alive);

    game_init(&g, 1);
    g.spawn_timer = 1000.0f;
    g.player.lane = 0;
    ang_speed = (half + 0.20f) / (0.40f - meet);
    g.enemies[0].kind = ENEMY_FLYER;
    g.enemies[0].alive = true;
    g.enemies[0].dist = 0.40f;
    g.enemies[0].speed = 0.0f;
    g.enemies[0].ang_speed = ang_speed;
    g.enemies[0].angle = center - ang_speed * meet;
    energy = g.energy;
    memset(&in, 0, sizeof in);
    in.fire = true;
    game_update(&g, &in, 0.40f);
    CHECK(!g.enemies[0].alive);
    CHECK(g.score == (uint32_t)STARPOST_SCORE_FLYER * (uint32_t)STARPOST_FLYER_SHOT_MULT);
    CHECK(g.energy == energy - t->shot_cost + STARPOST_FLYER_SHOT_MULT * t->flyer_energy);

    game_init(&g, 1);
    g.spawn_timer = 1000.0f;
    g.player.lane = 0;
    ang_speed = -(half + 0.20f) / (0.40f - meet);
    g.enemies[0].kind = ENEMY_FLYER;
    g.enemies[0].alive = true;
    g.enemies[0].dist = 0.40f;
    g.enemies[0].speed = 0.0f;
    g.enemies[0].ang_speed = ang_speed;
    g.enemies[0].angle = center - ang_speed * 0.40f;
    memset(&in, 0, sizeof in);
    in.fire = true;
    game_update(&g, &in, 0.40f);
    CHECK(g.enemies[0].alive);
    CHECK(g.score == 0);

    game_init(&g, 1);
    g.spawn_timer = 1000.0f;
    g.energy = t->zap_cost;
    g.enemies[0].kind = ENEMY_FLYER;
    g.enemies[0].alive = true;
    g.enemies[0].dist = 0.50f;
    g.enemies[0].speed = 0.0f;
    g.enemies[0].angle = center;
    g.enemies[0].ang_speed = 0.0f;
    memset(&in, 0, sizeof in);
    in.zap = true;
    game_update(&g, &in, STARPOST_DT);
    CHECK(!g.enemies[0].alive);
    CHECK(g.score == (uint32_t)STARPOST_SCORE_FLYER);
    CHECK(g.energy == t->flyer_energy);

    game_init(&g, 1);
    g.spawn_timer = 1000.0f;
    g.player.lane = 0;
    g.energy = 40;
    place(&g, 0, 0, 0.40f, 0.0f);
    place(&g, 1, 3, 0.40f, 0.0f);
    memset(&in, 0, sizeof in);
    in.energize = true;
    game_update(&g, &in, STARPOST_DT);
    CHECK(!g.enemies[0].alive);
    CHECK(g.enemies[1].alive);
    CHECK(g.energize_left[0] == t->energize_time);
    CHECK(g.score == (uint32_t)STARPOST_SCORE_KILL);
    energy = g.energy;
    in.energize = false;
    game_update(&g, &in, STARPOST_DT);
    g.player.lane = 3;
    in.energize = true;
    game_update(&g, &in, STARPOST_DT);
    CHECK(!g.enemies[1].alive);
    CHECK(g.energize_left[0] > 0.0f);
    CHECK(g.energize_left[3] == t->energize_time);
    CHECK(g.energy == energy - t->energize_cost + t->walker_energy);

    g.energize_left[0] = 0.10f;
    g.energize_left[3] = 2.0f;
    place(&g, 2, 0, 0.40f, 0.0f);
    place(&g, 3, 3, 0.40f, 0.0f);
    in.energize = false;
    game_update(&g, &in, 0.20f);
    CHECK(g.energize_left[0] == 0.0f);
    CHECK(g.energize_left[3] > 1.7f);
    CHECK(g.energize_left[3] < 1.9f);
    CHECK(g.enemies[2].alive);
    CHECK(!g.enemies[3].alive);

    game_init(&g, 1);
    g.spawn_timer = 1000.0f;
    g.player.lane = 1;
    g.energy = 40;
    memset(&in, 0, sizeof in);
    in.energize = true;
    game_update(&g, &in, STARPOST_DT);
    CHECK(g.energize_left[1] == t->energize_time);
    in.energize = false;
    game_update(&g, &in, 1.0f);
    CHECK(g.energize_left[1] == t->energize_time - 1.0f);
    in.energize = true;
    game_update(&g, &in, STARPOST_DT);
    CHECK(g.energize_left[1] == t->energize_time);
    CHECK(g.energy == 40 - 2 * t->energize_cost);
}

static void test_frame_step_cap(void)
{
    double acc = 0.0;
    int steps;

    CHECK(STARPOST_MAX_SIM_STEPS == 8);
    CHECK(STARPOST_MAX_FRAME_SEC == 0.25);

    steps = game_frame_steps(&acc, 10.0);
    CHECK(steps == STARPOST_MAX_SIM_STEPS);
    CHECK(acc > 0.0);
    CHECK(acc < STARPOST_MAX_FRAME_SEC);

    acc = 0.0;
    steps = game_frame_steps(&acc, (double)STARPOST_DT);
    CHECK(steps == 1);
    CHECK(acc < 1e-9);
    CHECK(acc > -1e-9);

    acc = 0.0;
    steps = game_frame_steps(&acc, -5.0);
    CHECK(steps == 0);
    CHECK(acc == 0.0);

    acc = 0.0;
    steps = game_frame_steps(NULL, 1.0);
    CHECK(steps == 0);
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
    test_flyer_lane_and_spiral();
    test_flyer_hit_window_and_damage();
    test_superzap_charges();
    test_held_fire_interval();
    test_wave_gauge();
    test_energy_abilities();
    test_splitter();
    test_projectiles_and_lanes();
    test_frame_step_cap();
    if (fails) {
        fprintf(stderr, "%d/%d checks failed\n", fails, checks);
        return 1;
    }
    printf("%d checks passed\n", checks);
    return 0;
}
