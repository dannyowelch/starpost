#include "game.h"

#include <string.h>

/* One table. Later waves are longer (more walkers, a longer spawn
   window), faster, and denser (shorter interval) until the caps. */
static const struct {
    int count0;
    int count_step;
    int count_max;
    float speed0;
    float speed_step;
    float speed_max;
    float interval0;
    float interval_step;
    float interval_min;
} WAVE_TABLE = {
    5, 3, 48,
    0.15f, 0.025f, 0.70f,
    0.90f, 0.04f, 0.22f,
};

static uint32_t rng_next(uint32_t *s)
{
    uint32_t x = *s;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *s = x;
    return x;
}

WaveConfig wave_config(int wave)
{
    WaveConfig c;
    int i;

    if (wave < 1)
        wave = 1;
    i = wave - 1;
    c.wave = wave;
    c.enemy_count = WAVE_TABLE.count0 + i * WAVE_TABLE.count_step;
    if (c.enemy_count > WAVE_TABLE.count_max)
        c.enemy_count = WAVE_TABLE.count_max;
    if (c.enemy_count > STARPOST_MAX_ENEMIES)
        c.enemy_count = STARPOST_MAX_ENEMIES;
    c.enemy_speed = WAVE_TABLE.speed0 + (float)i * WAVE_TABLE.speed_step;
    if (c.enemy_speed > WAVE_TABLE.speed_max)
        c.enemy_speed = WAVE_TABLE.speed_max;
    c.spawn_interval = WAVE_TABLE.interval0 - (float)i * WAVE_TABLE.interval_step;
    if (c.spawn_interval < WAVE_TABLE.interval_min)
        c.spawn_interval = WAVE_TABLE.interval_min;
    return c;
}

static void begin_wave(GameState *g, int wave)
{
    memset(g->enemies, 0, sizeof g->enemies);
    g->cfg = wave_config(wave);
    g->spawned = 0;
    g->killed = 0;
    g->spawn_timer = g->cfg.spawn_interval;
    g->clear_timer = 0.0f;
    g->player.beam_timer = 0.0f;
    g->state = STATE_PLAYING;
}

void game_init(GameState *g, uint32_t seed)
{
    memset(g, 0, sizeof *g);
    if (seed == 0)
        seed = 1;
    g->rng = seed;
    g->shield = STARPOST_SHIELD_MAX;
    g->shield_max = STARPOST_SHIELD_MAX;
    begin_wave(g, 1);
}

static void step_lane(GameState *g, int dir)
{
    int lane = (int)g->player.lane + dir;
    if (lane < 0)
        lane = STARPOST_LANES - 1;
    else if (lane >= STARPOST_LANES)
        lane = 0;
    g->player.lane = (uint8_t)lane;
}

static void turn_axis(GameState *g, bool held, bool *prev, int dir, float dt)
{
    if (!held) {
        *prev = false;
        return;
    }
    if (!*prev) {
        step_lane(g, dir);
        g->turn_timer = STARPOST_TURN_REPEAT;
    } else {
        g->turn_timer -= dt;
        if (g->turn_timer <= 0.0f) {
            step_lane(g, dir);
            g->turn_timer = STARPOST_TURN_REPEAT;
        }
    }
    *prev = true;
}

static void kill_nearest(GameState *g)
{
    int best = -1;
    float best_dist = 0.0f;
    for (int i = 0; i < STARPOST_MAX_ENEMIES; i++) {
        Enemy *e = &g->enemies[i];
        if (!e->alive || e->lane != g->player.lane)
            continue;
        if (best < 0 || e->dist < best_dist) {
            best = i;
            best_dist = e->dist;
        }
    }
    if (best < 0)
        return;
    g->enemies[best].alive = false;
    g->score += (uint32_t)STARPOST_SCORE_KILL;
    g->killed += 1;
}

static int spawn_walker(GameState *g)
{
    int slot = -1;
    for (int i = 0; i < STARPOST_MAX_ENEMIES; i++) {
        if (!g->enemies[i].alive) {
            slot = i;
            break;
        }
    }
    if (slot < 0)
        return 0;
    Enemy *e = &g->enemies[slot];
    e->kind = ENEMY_WALKER;
    e->lane = (uint8_t)(rng_next(&g->rng) % STARPOST_LANES);
    e->dist = 1.0f;
    e->speed = g->cfg.enemy_speed;
    e->alive = true;
    return 1;
}

static int any_alive(const GameState *g)
{
    for (int i = 0; i < STARPOST_MAX_ENEMIES; i++) {
        if (g->enemies[i].alive)
            return 1;
    }
    return 0;
}

static void enter_clear(GameState *g)
{
    g->shield += STARPOST_SHIELD_RECHARGE;
    if (g->shield > g->shield_max)
        g->shield = g->shield_max;
    g->score += (uint32_t)STARPOST_SCORE_WAVE;
    g->state = STATE_WAVE_CLEAR;
    g->clear_timer = STARPOST_WAVE_CLEAR_TIME;
    g->player.beam_timer = 0.0f;
}

void game_update(GameState *g, const InputFrame *in, float dt)
{
    if (!g || !in)
        return;

    if (g->state == STATE_GAME_OVER) {
        if (in->confirm) {
            uint32_t next = g->rng;
            if (next == 0)
                next = 1;
            game_init(g, next);
        }
        return;
    }

    if (dt < 0.0f)
        dt = 0.0f;

    if (g->state == STATE_WAVE_CLEAR) {
        g->clear_timer -= dt;
        if (g->clear_timer <= 0.0f)
            begin_wave(g, g->cfg.wave + 1);
        return;
    }

    if (g->player.cooldown > 0.0f) {
        g->player.cooldown -= dt;
        if (g->player.cooldown < 0.0f)
            g->player.cooldown = 0.0f;
    }
    if (g->player.beam_timer > 0.0f) {
        g->player.beam_timer -= dt;
        if (g->player.beam_timer < 0.0f)
            g->player.beam_timer = 0.0f;
    }

    if (in->left && !in->right)
        turn_axis(g, true, &g->prev_left, -1, dt);
    else
        turn_axis(g, false, &g->prev_left, -1, dt);
    if (in->right && !in->left)
        turn_axis(g, true, &g->prev_right, 1, dt);
    else
        turn_axis(g, false, &g->prev_right, 1, dt);

    if (in->fire && g->player.cooldown <= 0.0f) {
        g->player.beam_timer = STARPOST_BEAM_TIME;
        g->player.beam_lane = g->player.lane;
        g->player.cooldown = STARPOST_FIRE_COOLDOWN;
        kill_nearest(g);
    }

    for (int i = 0; i < STARPOST_MAX_ENEMIES; i++) {
        Enemy *e = &g->enemies[i];
        if (!e->alive)
            continue;
        e->dist -= e->speed * dt;
        if (e->dist > 0.0f)
            continue;
        e->alive = false;
        g->shield -= STARPOST_SHIELD_DRAIN;
        if (g->shield <= 0) {
            g->shield = 0;
            g->state = STATE_GAME_OVER;
        }
    }

    if (g->state != STATE_PLAYING)
        return;

    g->spawn_timer -= dt;
    while (g->spawn_timer <= 0.0f && g->spawned < g->cfg.enemy_count) {
        if (!spawn_walker(g))
            break;
        g->spawned += 1;
        g->spawn_timer += g->cfg.spawn_interval;
    }

    if (g->spawned >= g->cfg.enemy_count && !any_alive(g))
        enter_clear(g);
}
