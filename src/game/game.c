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
    int splitter_start;
} WAVE_TABLE = {
    5, 3, 48,
    /* About 15% slower than the first table (0.15, 0.025). Cap stays 0.70. */
    0.15f * 0.85f, 0.025f * 0.85f, 0.70f,
    0.90f, 0.04f, 0.22f,
    2, 1, 2, 4,
    3,
};

static int splitter_count_for(int wave, int start);
static int clamp_splitters(int walkers, int flyers, int splits);

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
    c.splitter_count = splitter_count_for(wave, WAVE_TABLE.splitter_start);
    c.splitter_count = clamp_splitters(c.enemy_count, c.flyer_count, c.splitter_count);
    return c;
}

const EnergyTune *energy_tune(void)
{
    static const EnergyTune tune = {
        20,   /* start */
        100,  /* cap */
        10,   /* wave_grant */
        1,    /* shot_cost */
        1,    /* walker_energy */
        5,    /* flyer_energy */
        0,    /* free_shot_at_zero */
        20,   /* zap_cost */
        2,    /* spread_cost */
        5,    /* energize_cost */
        3.0f, /* energize_time */
        10,   /* slow_cost */
        10,   /* repair_cost */
        0.28f, /* repair_glow */
        5.0f, /* slow_time */
        0.5f, /* slow_factor, walkers only */
        0.16f, /* deny_time */
        0.22f, /* spread_time */
        1,    /* splitter_energy */
        1     /* split_energy */
    };
    return &tune;
}

int starpost_normal_shot_allowed(const EnergyTune *tune, int energy)
{
    if (!tune)
        return 0;
    if (energy >= tune->shot_cost)
        return 1;
    return tune->free_shot_at_zero && energy == 0;
}

const DiffTune *diff_tune(Difficulty diff)
{
    /* Advanced is the base table: scale 1, no count offset, flyers from
       wave 2. Shield no longer recharges on a clear. Beginner is slower,
       sparser, and later flyers and splitters. Hyper is the opposite. */
    static const DiffTune tunes[DIFF_COUNT] = {
        {0.75f, -2, 1.35f, 4, 0, 5},
        {1.00f, 0, 1.00f, 2, 0, 3},
        {1.35f, 3, 0.75f, 1, 0, 2},
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

static int splitter_count_for(int wave, int start)
{
    int n;
    if (wave < start)
        return 0;
    n = 1 + (wave - start) / 3;
    if (n > 3)
        n = 3;
    if (n < 0)
        n = 0;
    return n;
}

/* A live splitter plus two children needs two slots beyond the parent. */
static int clamp_splitters(int walkers, int flyers, int splits)
{
    int room = STARPOST_MAX_ENEMIES - walkers - flyers;
    int cap;
    if (room < 0)
        room = 0;
    cap = room / 2;
    if (splits > cap)
        splits = cap;
    if (splits < 0)
        splits = 0;
    return splits;
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
    c.splitter_count = clamp_splitters(c.enemy_count, c.flyer_count,
                                       splitter_count_for(c.wave, t->splitter_start));
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
    g->splitters_spawned = 0;
    g->killed = 0;
    g->arrived = 0;
    g->quota_skip = 0;
    g->spawn_timer = g->cfg.spawn_interval;
    g->clear_timer = 0.0f;
    memset(g->shots, 0, sizeof g->shots);
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
    quota = g->cfg.enemy_count + g->cfg.flyer_count + g->cfg.splitter_count * 3;
    if (quota < 1)
        return 0.0f;
    left = quota - (g->killed + g->arrived + g->quota_skip);
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

static void grant_energy(GameState *g, int amount)
{
    if (amount <= 0)
        return;
    g->energy += amount;
    if (g->energy > energy_tune()->cap)
        g->energy = energy_tune()->cap;
}

static int is_lane_enemy(EnemyKind kind)
{
    return kind == ENEMY_WALKER || kind == ENEMY_SPLITTER || kind == ENEMY_SPLIT;
}

static int shield_drain_for(EnemyKind kind)
{
    if (kind == ENEMY_FLYER)
        return STARPOST_FLYER_DRAIN;
    if (kind == ENEMY_SPLITTER)
        return STARPOST_SPLITTER_DRAIN;
    if (kind == ENEMY_SPLIT)
        return STARPOST_SPLIT_DRAIN;
    return STARPOST_SHIELD_DRAIN;
}

static void spawn_split_child(GameState *g, int lane, float dist);

/* Filled while an attack resolves, then spawned so the same shot cannot
   kill the pieces it just created. Superzap never queues a split. */
static struct {
    int lane;
    float dist;
} split_queue[16];
static int split_queue_n;

static void queue_split(GameState *g, int lane, float dist)
{
    if (split_queue_n >= (int)(sizeof split_queue / sizeof split_queue[0])) {
        g->quota_skip += 2;
        return;
    }
    split_queue[split_queue_n].lane = lane;
    split_queue[split_queue_n].dist = dist;
    split_queue_n++;
}

static void flush_splits(GameState *g)
{
    int n = split_queue_n;
    int i;
    split_queue_n = 0;
    for (i = 0; i < n; i++) {
        spawn_split_child(g, split_queue[i].lane - 1, split_queue[i].dist);
        spawn_split_child(g, split_queue[i].lane + 1, split_queue[i].dist);
    }
}

static void kill_enemy(GameState *g, Enemy *e, int allow_split, int from_shot)
{
    const EnergyTune *tune;
    EnemyKind kind;
    int lane;
    float dist;
    int mult;
    if (!e || !e->alive)
        return;
    kind = e->kind;
    lane = e->lane;
    dist = e->dist;
    e->alive = false;
    g->killed += 1;
    tune = energy_tune();
    if (kind == ENEMY_FLYER) {
        mult = from_shot ? STARPOST_FLYER_SHOT_MULT : 1;
        g->score += (uint32_t)STARPOST_SCORE_FLYER * (uint32_t)mult;
        grant_energy(g, tune->flyer_energy * mult);
        emit(g, GAME_SND_FLYER_KILL);
    } else if (kind == ENEMY_SPLITTER) {
        g->score += (uint32_t)STARPOST_SCORE_SPLITTER;
        grant_energy(g, tune->splitter_energy);
        if (allow_split) {
            emit(g, GAME_SND_SPLIT);
            queue_split(g, lane, dist);
        } else {
            g->quota_skip += 2;
        }
    } else if (kind == ENEMY_SPLIT) {
        g->score += (uint32_t)STARPOST_SCORE_SPLIT;
        grant_energy(g, tune->split_energy);
        emit(g, GAME_SND_SPLIT_KILL);
    } else {
        g->score += (uint32_t)STARPOST_SCORE_KILL;
        grant_energy(g, tune->walker_energy);
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

static float enemy_step_speed(const GameState *g, const Enemy *e)
{
    float speed = e->speed;
    if (g->slow_timer > 0.0f && is_lane_enemy(e->kind))
        speed *= energy_tune()->slow_factor;
    return speed;
}

static int spawn_shot(GameState *g, int lane, int spread)
{
    int i;
    for (i = 0; i < STARPOST_MAX_SHOTS; i++) {
        if (g->shots[i].alive)
            continue;
        g->shots[i].alive = true;
        g->shots[i].lane = (uint8_t)wrap_lane(lane);
        g->shots[i].dist = 0.0f;
        g->shots[i].spread = spread ? 1 : 0;
        return 1;
    }
    return 0;
}

/* Leading shots claim the nearer enemy. A meeting time in [0, dt] is a hit,
   so a bullet cannot jump past a target that crosses its radius this step. */
static void advance_shots(GameState *g, float dt)
{
    int order[STARPOST_MAX_SHOTS];
    int n = 0;
    int i;
    if (dt <= 0.0f)
        return;
    for (i = 0; i < STARPOST_MAX_SHOTS; i++) {
        if (g->shots[i].alive)
            order[n++] = i;
    }
    for (i = 1; i < n; i++) {
        int key = order[i];
        int j = i - 1;
        while (j >= 0 && g->shots[order[j]].dist < g->shots[key].dist) {
            order[j + 1] = order[j];
            j--;
        }
        order[j + 1] = key;
    }
    for (i = 0; i < n; i++) {
        Shot *s = &g->shots[order[i]];
        float best_t = dt + 1.0f;
        int best = -1;
        int e;
        if (!s->alive)
            continue;
        for (e = 0; e < STARPOST_MAX_ENEMIES; e++) {
            Enemy *en = &g->enemies[e];
            float espeed;
            float rel;
            float t;
            float meet;
            if (!en->alive)
                continue;
            espeed = enemy_step_speed(g, en);
            rel = STARPOST_SHOT_SPEED + espeed;
            if (rel <= 0.0f)
                continue;
            t = (en->dist - s->dist) / rel;
            if (t < 0.0f || t > dt || t >= best_t)
                continue;
            meet = s->dist + STARPOST_SHOT_SPEED * t;
            if (meet > 1.0f)
                continue;
            if (is_lane_enemy(en->kind)) {
                if ((int)en->lane != (int)s->lane)
                    continue;
            } else if (en->kind == ENEMY_FLYER) {
                float ang = en->angle + en->ang_speed * t;
                if (!starpost_flyer_in_lane(ang, s->lane))
                    continue;
            } else {
                continue;
            }
            best_t = t;
            best = e;
        }
        if (best >= 0) {
            s->alive = false;
            kill_enemy(g, &g->enemies[best], 1, 1);
        } else {
            s->dist += STARPOST_SHOT_SPEED * dt;
            if (s->dist >= 1.0f)
                s->alive = false;
        }
    }
    flush_splits(g);
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
        kill_enemy(g, &g->enemies[i], 0, 0);
    }
    flush_splits(g);
    g->zap_flash = STARPOST_ZAP_FLASH;
    emit(g, GAME_SND_ZAP);
}

static void spread_shot(GameState *g)
{
    const EnergyTune *tune = energy_tune();
    int center = g->player.lane;
    if (!spend_energy(g, tune->spread_cost))
        return;
    emit(g, GAME_SND_SPREAD);
    spawn_shot(g, center - 1, 1);
    spawn_shot(g, center, 1);
    spawn_shot(g, center + 1, 1);
}

static int lane_energized(const GameState *g, int lane)
{
    if (lane < 0 || lane >= STARPOST_LANES)
        return 0;
    return g->energize_left[lane] > 0.0f;
}

static void sweep_energize(GameState *g)
{
    int lane;
    for (lane = 0; lane < STARPOST_LANES; lane++) {
        int i;
        if (!lane_energized(g, lane))
            continue;
        for (i = 0; i < STARPOST_MAX_ENEMIES; i++) {
            Enemy *e = &g->enemies[i];
            if (!e->alive)
                continue;
            if (is_lane_enemy(e->kind) && (int)e->lane == lane)
                kill_enemy(g, e, 1, 0);
            else if (e->kind == ENEMY_FLYER && starpost_flyer_in_lane(e->angle, lane))
                kill_enemy(g, e, 0, 0);
        }
    }
    flush_splits(g);
}

static void energize_lane(GameState *g)
{
    const EnergyTune *tune = energy_tune();
    int lane;
    if (!spend_energy(g, tune->energize_cost))
        return;
    lane = g->player.lane;
    /* A second press on a live lane replaces the remaining time. It still costs 5. */
    g->energize_left[lane] = tune->energize_time;
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

static int free_slot(const GameState *g)
{
    int i;
    for (i = 0; i < STARPOST_MAX_ENEMIES; i++) {
        if (!g->enemies[i].alive)
            return i;
    }
    return -1;
}

static void spawn_split_child(GameState *g, int lane, float dist)
{
    int slot = free_slot(g);
    Enemy *e;
    lane = wrap_lane(lane);
    if (dist < STARPOST_SPLIT_MIN_DIST)
        dist = STARPOST_SPLIT_MIN_DIST;
    if (slot < 0) {
        g->quota_skip += 1;
        return;
    }
    e = &g->enemies[slot];
    memset(e, 0, sizeof *e);
    e->kind = ENEMY_SPLIT;
    e->lane = (uint8_t)lane;
    e->dist = dist;
    e->speed = g->cfg.enemy_speed * STARPOST_SPLIT_SPEED_SCALE;
    e->alive = true;
    if (lane_energized(g, lane))
        kill_enemy(g, e, 0, 0);
}

static int spawn_walker(GameState *g)
{
    int slot = free_slot(g);
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

static int spawn_splitter(GameState *g)
{
    int slot = free_slot(g);
    Enemy *e;
    if (slot < 0)
        return 0;
    e = &g->enemies[slot];
    e->kind = ENEMY_SPLITTER;
    e->lane = (uint8_t)(rng_next(&g->rng) % STARPOST_LANES);
    e->dist = 1.0f;
    e->speed = g->cfg.enemy_speed * STARPOST_SPLITTER_SPEED_SCALE;
    e->angle = 0.0f;
    e->ang_speed = 0.0f;
    e->alive = true;
    g->splitters_spawned += 1;
    return 1;
}

static int spawn_flyer(GameState *g)
{
    int slot = free_slot(g);
    Enemy *e;
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
    int walkers = g->spawned - g->flyers_spawned - g->splitters_spawned;
    int flyers_left = g->cfg.flyer_count - g->flyers_spawned;
    int splits_left = g->cfg.splitter_count - g->splitters_spawned;
    int walkers_left = g->cfg.enemy_count - walkers;
    int flyer = 0;
    if (splits_left > 0) {
        int total = g->cfg.enemy_count + g->cfg.flyer_count + g->cfg.splitter_count;
        int stride = total / g->cfg.splitter_count;
        if (stride < 1)
            stride = 1;
        if ((g->spawned % stride) == 0 && g->splitters_spawned <= g->spawned / stride)
            return spawn_splitter(g);
        if (walkers_left <= 0 && flyers_left <= 0)
            return spawn_splitter(g);
    }
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
    if (walkers_left > 0)
        return spawn_walker(g);
    if (splits_left > 0)
        return spawn_splitter(g);
    return spawn_flyer(g);
}

static int any_alive(const GameState *g)
{
    for (int i = 0; i < STARPOST_MAX_ENEMIES; i++) {
        if (g->enemies[i].alive)
            return 1;
    }
    return 0;
}

static void try_repair(GameState *g)
{
    const EnergyTune *tune = energy_tune();
    if (g->shield >= g->shield_max || g->energy < tune->repair_cost) {
        g->deny_flash = tune->deny_time;
        emit(g, GAME_SND_DENY);
        return;
    }
    g->energy -= tune->repair_cost;
    g->shield += 1;
    g->repair_flash = tune->repair_glow;
    emit(g, GAME_SND_REPAIR);
}

static void enter_clear(GameState *g)
{
    g->score += (uint32_t)STARPOST_SCORE_WAVE;
    grant_energy(g, energy_tune()->wave_grant);
    g->state = STATE_WAVE_CLEAR;
    g->clear_timer = STARPOST_WAVE_CLEAR_TIME;
    memset(g->shots, 0, sizeof g->shots);
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
    {
        int lane;
        for (lane = 0; lane < STARPOST_LANES; lane++) {
            if (g->energize_left[lane] <= 0.0f)
                continue;
            g->energize_left[lane] -= dt;
            if (g->energize_left[lane] < 0.0f)
                g->energize_left[lane] = 0.0f;
        }
    }
    if (g->slow_timer > 0.0f) {
        g->slow_timer -= dt;
        if (g->slow_timer < 0.0f)
            g->slow_timer = 0.0f;
    }
    if (g->repair_flash > 0.0f) {
        g->repair_flash -= dt;
        if (g->repair_flash < 0.0f)
            g->repair_flash = 0.0f;
    }

    if (in->repair && !g->prev_repair)
        try_repair(g);
    g->prev_repair = in->repair;

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
    if (in->left && !in->right)
        turn_axis(g, true, &g->prev_left, -1, dt);
    else
        turn_axis(g, false, &g->prev_left, -1, dt);
    if (in->right && !in->left)
        turn_axis(g, true, &g->prev_right, 1, dt);
    else
        turn_axis(g, false, &g->prev_right, 1, dt);

    if (in->fire && g->player.cooldown <= 0.0f) {
        const EnergyTune *tune = energy_tune();
        if (!starpost_normal_shot_allowed(tune, g->energy)) {
            g->deny_flash = tune->deny_time;
            g->player.cooldown = STARPOST_FIRE_COOLDOWN;
            emit(g, GAME_SND_DENY);
        } else {
            if (!(tune->free_shot_at_zero && g->energy == 0))
                g->energy -= tune->shot_cost;
            g->player.cooldown = STARPOST_FIRE_COOLDOWN;
            emit(g, GAME_SND_FIRE);
            spawn_shot(g, g->player.lane, 0);
        }
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
    advance_shots(g, dt);

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
        if (g->slow_timer > 0.0f && is_lane_enemy(e->kind))
            speed *= energy_tune()->slow_factor;
        e->dist -= speed * dt;
        if (e->dist > 0.0f)
            continue;
        e->alive = false;
        g->arrived += 1;
        if (e->kind == ENEMY_SPLITTER)
            g->quota_skip += 2;
        drain = shield_drain_for(e->kind);
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
           g->spawned < g->cfg.enemy_count + g->cfg.flyer_count + g->cfg.splitter_count) {
        if (!spawn_next(g))
            break;
        g->spawned += 1;
        g->spawn_timer += g->cfg.spawn_interval;
    }
    sweep_energize(g);

    if (g->spawned >= g->cfg.enemy_count + g->cfg.flyer_count + g->cfg.splitter_count &&
        !any_alive(g))
        enter_clear(g);
}
