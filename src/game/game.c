#include "game.h"

#include <math.h>
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
    int flyer_start;
    int flyer_base;
    int flyer_every;
    int flyer_max;
} WAVE_TABLE = {
    5, 3, 48,
    0.15f, 0.025f, 0.70f,
    0.90f, 0.04f, 0.22f,
    2, 1, 2, 4,
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
    c.flyer_count = 0;
    if (wave >= WAVE_TABLE.flyer_start) {
        int n = WAVE_TABLE.flyer_base + (wave - WAVE_TABLE.flyer_start) / WAVE_TABLE.flyer_every;
        if (n > WAVE_TABLE.flyer_max)
            n = WAVE_TABLE.flyer_max;
        if (n < 0)
            n = 0;
        c.flyer_count = n;
    }
    if (c.enemy_count + c.flyer_count > STARPOST_MAX_ENEMIES)
        c.flyer_count = STARPOST_MAX_ENEMIES - c.enemy_count;
    return c;
}

float starpost_lane_center(int lane)
{
    const float sector = 6.2831853f / (float)STARPOST_LANES;
    lane %= STARPOST_LANES;
    if (lane < 0)
        lane += STARPOST_LANES;
    return 1.5707963f - (float)lane * sector;
}

static float wrap_pi(float a)
{
    const float pi = 3.14159265f;
    const float twopi = 6.2831853f;
    a = fmodf(a, twopi);
    if (a > pi)
        a -= twopi;
    if (a < -pi)
        a += twopi;
    return a;
}

int starpost_lane_from_angle(float angle)
{
    const float sector = 6.2831853f / (float)STARPOST_LANES;
    float lanes = (1.5707963f - angle) / sector;
    int lane = (int)floorf(lanes + 0.5f);
    lane %= STARPOST_LANES;
    if (lane < 0)
        lane += STARPOST_LANES;
    return lane;
}

int starpost_flyer_in_lane(float angle, int lane)
{
    const float sector = 6.2831853f / (float)STARPOST_LANES;
    float half = 0.5f * STARPOST_HIT_WINDOW * sector;
    float d = wrap_pi(angle - starpost_lane_center(lane));
    if (d < 0.0f)
        d = -d;
    return d <= half;
}

static void begin_wave(GameState *g, int wave)
{
    memset(g->enemies, 0, sizeof g->enemies);
    g->cfg = wave_config(wave);
    g->spawned = 0;
    g->flyers_spawned = 0;
    g->killed = 0;
    g->spawn_timer = g->cfg.spawn_interval;
    g->clear_timer = 0.0f;
    g->player.beam_timer = 0.0f;
    if (g->player.zaps < STARPOST_ZAP_MAX)
        g->player.zaps = (uint8_t)(g->player.zaps + 1);
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

int game_frame_steps(double *acc, double elapsed)
{
    int steps = 0;

    if (!acc)
        return 0;
    if (elapsed < 0.0)
        elapsed = 0.0;
    if (elapsed > STARPOST_MAX_FRAME_SEC)
        elapsed = STARPOST_MAX_FRAME_SEC;
    *acc += elapsed;
    while (*acc >= (double)STARPOST_DT && steps < STARPOST_MAX_SIM_STEPS) {
        *acc -= (double)STARPOST_DT;
        steps++;
    }
    return steps;
}

static void emit(GameState *g, uint8_t id)
{
    if (g->snd_count >= GAME_SND_MAX)
        return;
    g->snd[g->snd_count++] = id;
}

static void step_lane(GameState *g, int dir)
{
    int lane = (int)g->player.lane + dir;
    if (lane < 0)
        lane = STARPOST_LANES - 1;
    else if (lane >= STARPOST_LANES)
        lane = 0;
    g->player.lane = (uint8_t)lane;
    emit(g, GAME_SND_STEP);
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

static int in_tracer(const GameState *g, const Enemy *e)
{
    if (e->kind == ENEMY_FLYER)
        return starpost_flyer_in_lane(e->angle, g->player.lane);
    return e->lane == g->player.lane;
}

static void kill_enemy(GameState *g, Enemy *e)
{
    if (!e->alive)
        return;
    e->alive = false;
    g->killed += 1;
    if (e->kind == ENEMY_FLYER) {
        g->score += (uint32_t)STARPOST_SCORE_FLYER;
        emit(g, GAME_SND_FLYER_KILL);
    } else {
        g->score += (uint32_t)STARPOST_SCORE_KILL;
        emit(g, GAME_SND_WALKER_KILL);
    }
}

static void kill_nearest(GameState *g)
{
    int best = -1;
    float best_dist = 0.0f;
    for (int i = 0; i < STARPOST_MAX_ENEMIES; i++) {
        Enemy *e = &g->enemies[i];
        if (!e->alive || !in_tracer(g, e))
            continue;
        if (best < 0 || e->dist < best_dist) {
            best = i;
            best_dist = e->dist;
        }
    }
    if (best >= 0)
        kill_enemy(g, &g->enemies[best]);
}

static void superzap(GameState *g)
{
    int any = 0;
    if (g->player.zaps == 0)
        return;
    for (int i = 0; i < STARPOST_MAX_ENEMIES; i++) {
        if (!g->enemies[i].alive)
            continue;
        kill_enemy(g, &g->enemies[i]);
        any = 1;
    }
    if (!any)
        return;
    g->player.zaps = (uint8_t)(g->player.zaps - 1);
    g->zap_flash = STARPOST_ZAP_FLASH;
    emit(g, GAME_SND_ZAP);
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
    e->angle = 0.0f;
    e->ang_speed = 0.0f;
    e->alive = true;
    return 1;
}

static int spawn_flyer(GameState *g)
{
    int slot = -1;
    Enemy *e;
    for (int i = 0; i < STARPOST_MAX_ENEMIES; i++) {
        if (!g->enemies[i].alive) {
            slot = i;
            break;
        }
    }
    if (slot < 0)
        return 0;
    e = &g->enemies[slot];
    e->kind = ENEMY_FLYER;
    e->angle = starpost_lane_center((int)(rng_next(&g->rng) % STARPOST_LANES));
    e->lane = (uint8_t)starpost_lane_from_angle(e->angle);
    e->dist = 1.0f;
    e->speed = g->cfg.enemy_speed * STARPOST_FLYER_SPEED_SCALE;
    e->ang_speed = STARPOST_FLYER_ANG_SPEED;
    e->alive = true;
    g->flyers_spawned += 1;
    emit(g, GAME_SND_FLYER_SPAWN);
    return 1;
}

static int spawn_next(GameState *g)
{
    int walkers = g->spawned - g->flyers_spawned;
    int flyers_left = g->cfg.flyer_count - g->flyers_spawned;
    int walkers_left = g->cfg.enemy_count - walkers;
    int flyer = 0;
    if (flyers_left > 0 && walkers_left <= 0) {
        flyer = 1;
    } else if (flyers_left > 0 && walkers_left > 0) {
        int stride = g->cfg.enemy_count / g->cfg.flyer_count;
        if (stride < 1)
            stride = 1;
        if ((walkers % stride) == 0 && g->flyers_spawned <= walkers / stride)
            flyer = 1;
    }
    if (flyer)
        return spawn_flyer(g);
    return spawn_walker(g);
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
    emit(g, GAME_SND_WAVE);
}

void game_update(GameState *g, const InputFrame *in, float dt)
{
    if (!g || !in)
        return;

    g->snd_count = 0;

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

    if (g->zap_flash > 0.0f) {
        g->zap_flash -= dt;
        if (g->zap_flash < 0.0f)
            g->zap_flash = 0.0f;
    }

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
        emit(g, GAME_SND_FIRE);
        kill_nearest(g);
    }
    if (in->zap && !g->prev_zap)
        superzap(g);
    g->prev_zap = in->zap;

    for (int i = 0; i < STARPOST_MAX_ENEMIES; i++) {
        Enemy *e = &g->enemies[i];
        int drain;
        if (!e->alive)
            continue;
        if (e->kind == ENEMY_FLYER) {
            e->angle += e->ang_speed * dt;
            e->lane = (uint8_t)starpost_lane_from_angle(e->angle);
        }
        e->dist -= e->speed * dt;
        if (e->dist > 0.0f)
            continue;
        e->alive = false;
        drain = (e->kind == ENEMY_FLYER) ? STARPOST_FLYER_DRAIN : STARPOST_SHIELD_DRAIN;
        g->shield -= drain;
        emit(g, GAME_SND_BASE);
        if (g->shield <= 0) {
            g->shield = 0;
            if (g->state != STATE_GAME_OVER) {
                g->state = STATE_GAME_OVER;
                emit(g, GAME_SND_OVER);
            }
        }
    }

    if (g->state != STATE_PLAYING)
        return;

    g->spawn_timer -= dt;
    while (g->spawn_timer <= 0.0f &&
           g->spawned < g->cfg.enemy_count + g->cfg.flyer_count) {
        if (!spawn_next(g))
            break;
        g->spawned += 1;
        g->spawn_timer += g->cfg.spawn_interval;
    }

    if (g->spawned >= g->cfg.enemy_count + g->cfg.flyer_count && !any_alive(g))
        enter_clear(g);
}
