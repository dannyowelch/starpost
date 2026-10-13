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

const EnergyTune *energy_tune(void)
{
    static const EnergyTune tune = {
        20,   /* start */
        100,  /* cap */
        20,   /* wave_grant */
        20,   /* zap_cost */
        1,    /* spread_cost */
        5,    /* energize_cost */
        3.0f, /* energize_time */
        10,   /* slow_cost */
        5.0f, /* slow_time */
        0.5f, /* slow_factor, walkers only */
        0.16f, /* deny_time */
        0.22f  /* spread_time */
    };
    return &tune;
}

const DiffTune *diff_tune(Difficulty diff)
{
    /* Advanced is the base table: scale 1, no count offset, flyers from
       wave 2, recharge STARPOST_SHIELD_RECHARGE. Beginner is slower,
       sparser, and later flyers. Hyper is the opposite. */
    static const DiffTune tunes[DIFF_COUNT] = {
        {0.75f, -2, 1.35f, 4, 3},
        {1.00f, 0, 1.00f, 2, STARPOST_SHIELD_RECHARGE},
        {1.35f, 3, 0.75f, 1, 1},
    };
    if (diff < 0 || diff >= DIFF_COUNT)
        diff = DIFF_ADVANCED;
    return &tunes[diff];
}

const char *diff_name(Difficulty diff)
{
    static const char *names[DIFF_COUNT] = {"BEGINNER", "ADVANCED", "HYPER"};
    if (diff < 0 || diff >= DIFF_COUNT)
        diff = DIFF_ADVANCED;
    return names[diff];
}

static int flyer_count_for(int wave, int start)
{
    int n;
    if (wave < start)
        return 0;
    n = 1 + (wave - start) / 2;
    if (n > 4)
        n = 4;
    if (n < 0)
        n = 0;
    return n;
}

WaveConfig wave_config_diff(int wave, Difficulty diff)
{
    const DiffTune *t = diff_tune(diff);
    WaveConfig c = wave_config(wave);
    int flyers;
    c.enemy_count += t->count_add;
    if (c.enemy_count < 1)
        c.enemy_count = 1;
    if (c.enemy_count > 48)
        c.enemy_count = 48;
    c.enemy_speed *= t->speed_scale;
    if (c.enemy_speed > 1.05f)
        c.enemy_speed = 1.05f;
    c.spawn_interval *= t->interval_scale;
    if (c.spawn_interval < 0.12f)
        c.spawn_interval = 0.12f;
    if (c.spawn_interval > 2.0f)
        c.spawn_interval = 2.0f;
    flyers = flyer_count_for(c.wave, t->flyer_start);
    if (c.enemy_count + flyers > STARPOST_MAX_ENEMIES)
        flyers = STARPOST_MAX_ENEMIES - c.enemy_count;
    c.flyer_count = flyers;
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
    g->cfg = wave_config_diff(wave, g->difficulty);
    g->spawned = 0;
    g->flyers_spawned = 0;
    g->killed = 0;
    g->arrived = 0;
    g->spawn_timer = g->cfg.spawn_interval;
    g->clear_timer = 0.0f;
    g->player.beam_timer = 0.0f;
    g->player.spread_timer = 0.0f;
    g->state = STATE_PLAYING;
}

void game_init_difficulty(GameState *g, uint32_t seed, Difficulty diff)
{
    if (!g)
        return;
    if (diff < 0 || diff >= DIFF_COUNT)
        diff = DIFF_ADVANCED;
    memset(g, 0, sizeof *g);
    if (seed == 0)
        seed = 1;
    g->rng = seed;
    g->difficulty = diff;
    g->shield = STARPOST_SHIELD_MAX;
    g->shield_max = STARPOST_SHIELD_MAX;
    g->shield_recharge = diff_tune(diff)->shield_recharge;
    g->energy = energy_tune()->start;
    if (g->energy > energy_tune()->cap)
        g->energy = energy_tune()->cap;
    g->energize_lane = 0;
    begin_wave(g, 1);
}

void game_init(GameState *g, uint32_t seed)
{
    game_init_difficulty(g, seed, DIFF_ADVANCED);
}

float starpost_wave_gauge(const GameState *g)
{
    int quota;
    int left;
    if (!g)
        return 0.0f;
    quota = g->cfg.enemy_count + g->cfg.flyer_count;
    if (quota < 1)
        return 0.0f;
    left = quota - (g->killed + g->arrived);
    if (left < 0)
        left = 0;
    return (float)left / (float)quota;
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

static int wrap_lane(int lane)
{
    lane %= STARPOST_LANES;
    if (lane < 0)
        lane += STARPOST_LANES;
    return lane;
}

static int nearest_walker(const GameState *g, int lane)
{
    int best = -1;
    float best_dist = 0.0f;
    for (int i = 0; i < STARPOST_MAX_ENEMIES; i++) {
        const Enemy *e = &g->enemies[i];
        if (!e->alive || e->kind != ENEMY_WALKER || (int)e->lane != lane)
            continue;
        if (best < 0 || e->dist < best_dist) {
            best = i;
            best_dist = e->dist;
        }
    }
    return best;
}

static void kill_nearest(GameState *g)
{
    int best = nearest_walker(g, g->player.lane);
    float best_dist = 0.0f;
    int flyer = -1;
    if (best >= 0)
        best_dist = g->enemies[best].dist;
    for (int i = 0; i < STARPOST_MAX_ENEMIES; i++) {
        Enemy *e = &g->enemies[i];
        if (!e->alive || e->kind != ENEMY_FLYER)
            continue;
        if (!starpost_flyer_in_lane(e->angle, g->player.lane))
            continue;
        if (best < 0 || e->dist < best_dist) {
            flyer = i;
            best = -1;
            best_dist = e->dist;
        }
    }
    if (flyer >= 0)
        kill_enemy(g, &g->enemies[flyer]);
    else if (best >= 0)
        kill_enemy(g, &g->enemies[best]);
}

static int spend_energy(GameState *g, int cost)
{
    if (g->energy < cost) {
        g->deny_flash = energy_tune()->deny_time;
        emit(g, GAME_SND_DENY);
        return 0;
    }
    g->energy -= cost;
    return 1;
}

static void superzap(GameState *g)
{
    int any = 0;
    const EnergyTune *tune = energy_tune();
    for (int i = 0; i < STARPOST_MAX_ENEMIES; i++) {
        if (g->enemies[i].alive) {
            any = 1;
            break;
        }
    }
    if (!any)
        return;
    if (!spend_energy(g, tune->zap_cost))
        return;
    for (int i = 0; i < STARPOST_MAX_ENEMIES; i++) {
        if (!g->enemies[i].alive)
            continue;
        kill_enemy(g, &g->enemies[i]);
    }
    g->zap_flash = STARPOST_ZAP_FLASH;
    emit(g, GAME_SND_ZAP);
}

static void spread_shot(GameState *g)
{
    const EnergyTune *tune = energy_tune();
    int center = g->player.lane;
    int lanes[3];
    if (!spend_energy(g, tune->spread_cost))
        return;
    lanes[0] = wrap_lane(center - 1);
    lanes[1] = center;
    lanes[2] = wrap_lane(center + 1);
    g->player.spread_lane = (uint8_t)center;
    g->player.spread_timer = tune->spread_time;
    emit(g, GAME_SND_SPREAD);
    for (int n = 0; n < 3; n++) {
        int slot = nearest_walker(g, lanes[n]);
        if (slot >= 0)
            kill_enemy(g, &g->enemies[slot]);
        for (int i = 0; i < STARPOST_MAX_ENEMIES; i++) {
            Enemy *e = &g->enemies[i];
            if (!e->alive || e->kind != ENEMY_FLYER)
                continue;
            if (starpost_flyer_in_lane(e->angle, lanes[n]))
                kill_enemy(g, e);
        }
    }
}

static void sweep_energize(GameState *g)
{
    if (g->energize_timer <= 0.0f)
        return;
    for (int i = 0; i < STARPOST_MAX_ENEMIES; i++) {
        Enemy *e = &g->enemies[i];
        if (!e->alive)
            continue;
        if (e->kind == ENEMY_WALKER && (int)e->lane == g->energize_lane)
            kill_enemy(g, e);
        else if (e->kind == ENEMY_FLYER &&
                 starpost_flyer_in_lane(e->angle, g->energize_lane))
            kill_enemy(g, e);
    }
}

static void energize_lane(GameState *g)
{
    const EnergyTune *tune = energy_tune();
    if (!spend_energy(g, tune->energize_cost))
        return;
    g->energize_lane = g->player.lane;
    g->energize_timer = tune->energize_time;
    emit(g, GAME_SND_ENERGIZE);
    sweep_energize(g);
}

static void slow_enemies(GameState *g)
{
    const EnergyTune *tune = energy_tune();
    if (!spend_energy(g, tune->slow_cost))
        return;
    g->slow_timer = tune->slow_time;
    emit(g, GAME_SND_SLOW);
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
    g->shield += g->shield_recharge;
    if (g->shield > g->shield_max)
        g->shield = g->shield_max;
    g->score += (uint32_t)STARPOST_SCORE_WAVE;
    g->energy += energy_tune()->wave_grant;
    if (g->energy > energy_tune()->cap)
        g->energy = energy_tune()->cap;
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
    if (g->deny_flash > 0.0f) {
        g->deny_flash -= dt;
        if (g->deny_flash < 0.0f)
            g->deny_flash = 0.0f;
    }
    if (g->player.spread_timer > 0.0f) {
        g->player.spread_timer -= dt;
        if (g->player.spread_timer < 0.0f)
            g->player.spread_timer = 0.0f;
    }
    if (g->energize_timer > 0.0f) {
        g->energize_timer -= dt;
        if (g->energize_timer < 0.0f)
            g->energize_timer = 0.0f;
    }
    if (g->slow_timer > 0.0f) {
        g->slow_timer -= dt;
        if (g->slow_timer < 0.0f)
            g->slow_timer = 0.0f;
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
    if (in->spread && !g->prev_spread)
        spread_shot(g);
    if (in->energize && !g->prev_energize)
        energize_lane(g);
    if (in->slow && !g->prev_slow)
        slow_enemies(g);
    if (in->zap && !g->prev_zap)
        superzap(g);
    g->prev_spread = in->spread;
    g->prev_energize = in->energize;
    g->prev_slow = in->slow;
    g->prev_zap = in->zap;
    sweep_energize(g);

    for (int i = 0; i < STARPOST_MAX_ENEMIES; i++) {
        Enemy *e = &g->enemies[i];
        float speed;
        int drain;
        if (!e->alive)
            continue;
        if (e->kind == ENEMY_FLYER) {
            e->angle += e->ang_speed * dt;
            e->lane = (uint8_t)starpost_lane_from_angle(e->angle);
        }
        speed = e->speed;
        if (g->slow_timer > 0.0f && e->kind == ENEMY_WALKER)
            speed *= energy_tune()->slow_factor;
        e->dist -= speed * dt;
        if (e->dist > 0.0f)
            continue;
        e->alive = false;
        g->arrived += 1;
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
    sweep_energize(g);

    if (g->spawned >= g->cfg.enemy_count + g->cfg.flyer_count && !any_alive(g))
        enter_clear(g);
}
