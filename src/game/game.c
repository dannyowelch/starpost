#include "game.h"

#include <string.h>

static const float WALKER_SPEED = 0.20f;
static const float SPAWN_INTERVAL = 0.50f;

static uint32_t rng_next(uint32_t *s)
{
    uint32_t x = *s;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *s = x;
    return x;
}

void game_init(GameState *g, uint32_t seed)
{
    memset(g, 0, sizeof *g);
    if (seed == 0)
        seed = 1;
    g->rng = seed;
    g->shield = STARPOST_SHIELD_MAX;
    g->shield_max = STARPOST_SHIELD_MAX;
    g->spawn_timer = SPAWN_INTERVAL;
    g->state = STATE_PLAYING;
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
    if (best >= 0)
        g->enemies[best].alive = false;
}

static void spawn_walker(GameState *g)
{
    int slot = -1;
    for (int i = 0; i < STARPOST_MAX_ENEMIES; i++) {
        if (!g->enemies[i].alive) {
            slot = i;
            break;
        }
    }
    if (slot < 0)
        return;
    Enemy *e = &g->enemies[slot];
    e->kind = ENEMY_WALKER;
    e->lane = (uint8_t)(rng_next(&g->rng) % STARPOST_LANES);
    e->dist = 1.0f;
    e->speed = WALKER_SPEED;
    e->alive = true;
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
    while (g->spawn_timer <= 0.0f) {
        g->spawn_timer += SPAWN_INTERVAL;
        spawn_walker(g);
    }
}
