#ifndef STARPOST_GAME_H
#define STARPOST_GAME_H

#include <stdbool.h>
#include <stdint.h>

#define STARPOST_LANES 8
#define STARPOST_MAX_ENEMIES 64
#define STARPOST_SHIELD_MAX 8
#define STARPOST_SHIELD_DRAIN 1
#define STARPOST_SHIELD_RECHARGE 0
#define STARPOST_SCORE_KILL 100
#define STARPOST_SCORE_FLYER 300
#define STARPOST_SCORE_SPLITTER 200
#define STARPOST_SCORE_SPLIT 50
#define STARPOST_SCORE_WAVE 500
#define STARPOST_FLYER_DRAIN 3
#define STARPOST_SPLITTER_DRAIN 2
#define STARPOST_SPLIT_DRAIN 1
#define STARPOST_ZAP_MAX 4
#define STARPOST_ZAP_FLASH 0.28f
/* Fraction of one lane sector, centered on the tracer. 1 covers the
   whole sector; smaller leaves a gap so a flyer between lanes is missed. */
#define STARPOST_HIT_WINDOW 0.55f
#define STARPOST_FLYER_ANG_SPEED 1.8f
#define STARPOST_FLYER_SPEED_SCALE 0.72f
/* Parent is slower than a walker. Children are faster, so the split is urgent. */
#define STARPOST_SPLITTER_SPEED_SCALE 0.62f
#define STARPOST_SPLIT_SPEED_SCALE 1.25f
#define STARPOST_SPLIT_MIN_DIST 0.04f
/* Radians per second along the rim while an enemy is waiting to enter. */
#define STARPOST_RING_WANDER 0.85f
#define STARPOST_WAVE_CLEAR_TIME 1.5f
#define STARPOST_DT (1.0f / 120.0f)
#define STARPOST_MAX_SHOTS 32
/* Crosses the lane in 0.4 s. Fast enough to answer the fire button, slow enough to see. */
#define STARPOST_SHOT_SPEED 2.5f
#define STARPOST_FIRE_COOLDOWN 0.16f
/* A flyer killed by a normal or spread projectile scores and pays this many times. */
#define STARPOST_FLYER_SHOT_MULT 3
#define STARPOST_TURN_REPEAT 0.13f
/* A slow frame must not replay an unbounded stretch of simulation. */
#define STARPOST_MAX_FRAME_SEC 0.25
#define STARPOST_MAX_SIM_STEPS 8

/* One table for the energy prototype. Slow's factor applies to lane enemies
   (walkers, splitters, and the small units), not flyers.
   free_shot_at_zero: 0 denies a normal shot at 0 energy; 1 fires it free. */
typedef struct {
    int start;
    int cap;
    int wave_grant;
    int shot_cost;
    int walker_energy;
    int flyer_energy;
    int free_shot_at_zero;
    int zap_cost;
    int spread_cost;
    int energize_cost;
    float energize_time;
    int slow_cost;
    int repair_cost;
    float repair_glow;
    float slow_time;
    float slow_factor;
    float deny_time;
    float spread_time;
    int splitter_energy;
    int split_energy;
} EnergyTune;

typedef enum {
    ENEMY_NONE = 0,
    ENEMY_WALKER = 1,
    ENEMY_FLYER = 2,
    ENEMY_SPLITTER = 3,
    ENEMY_SPLIT = 4
} EnemyKind;

typedef enum {
    STATE_PLAYING = 0,
    STATE_GAME_OVER = 1,
    STATE_WAVE_CLEAR = 2
} GamePlayState;

typedef struct {
    EnemyKind kind;
    uint8_t lane;
    float dist;
    float speed;
    float angle;
    float ang_speed;
    bool alive;
    bool on_ring;
} Enemy;

typedef struct {
    float dist;
    uint8_t lane;
    uint8_t spread;
    bool alive;
} Shot;

typedef struct {
    uint8_t lane;
    float cooldown;
} Player;

typedef struct {
    bool left;
    bool right;
    bool fire;
    bool zap;
    bool spread;
    bool energize;
    bool slow;
    bool repair;
    bool confirm;
} InputFrame;

typedef enum {
    DIFF_BEGINNER = 0,
    DIFF_ADVANCED = 1,
    DIFF_HYPER = 2,
    DIFF_COUNT = 3
} Difficulty;

typedef struct {
    float speed_scale;
    int count_add;
    float interval_scale;
    int flyer_start;
    int shield_recharge;
    int splitter_start;
} DiffTune;

typedef struct {
    int wave;
    int enemy_count;
    int flyer_count;
    int splitter_count;
    float spawn_interval;
    float enemy_speed;
} WaveConfig;

typedef enum {
    GAME_SND_STEP = 1,
    GAME_SND_FIRE,
    GAME_SND_WALKER_KILL,
    GAME_SND_FLYER_SPAWN,
    GAME_SND_FLYER_KILL,
    GAME_SND_BASE,
    GAME_SND_ZAP,
    GAME_SND_WAVE,
    GAME_SND_OVER,
    GAME_SND_SPREAD,
    GAME_SND_ENERGIZE,
    GAME_SND_SLOW,
    GAME_SND_DENY,
    GAME_SND_REPAIR,
    GAME_SND_SPLIT,
    GAME_SND_SPLIT_KILL
} GameSnd;

#define GAME_SND_MAX 24

typedef struct {
    Enemy enemies[STARPOST_MAX_ENEMIES];
    Shot shots[STARPOST_MAX_SHOTS];
    Player player;
    WaveConfig cfg;
    int spawned;
    int flyers_spawned;
    int splitters_spawned;
    int killed;
    int arrived;
    /* Children reserved in the quota that will never spawn (zap, base, or a full board). */
    int quota_skip;
    float spawn_timer;
    float clear_timer;
    float turn_timer;
    float zap_flash;
    float deny_flash;
    float repair_flash;
    float energize_left[STARPOST_LANES];
    float slow_timer;
    int energy;
    bool prev_left;
    bool prev_right;
    bool prev_zap;
    bool prev_spread;
    bool prev_energize;
    bool prev_slow;
    bool prev_repair;
    int shield;
    int shield_max;
    int shield_recharge;
    Difficulty difficulty;
    uint32_t score;
    uint32_t rng;
    int demo_tick;
    GamePlayState state;
    uint8_t snd_count;
    uint8_t snd[GAME_SND_MAX];
} GameState;

const EnergyTune *energy_tune(void);
/* 1 when a normal shot may fire. At 0 energy this is free_shot_at_zero. */
int starpost_normal_shot_allowed(const EnergyTune *tune, int energy);
WaveConfig wave_config(int wave);
const DiffTune *diff_tune(Difficulty diff);
const char *diff_name(Difficulty diff);
WaveConfig wave_config_diff(int wave, Difficulty diff);
float starpost_lane_center(int lane);
int starpost_lane_from_angle(float angle);
int starpost_flyer_in_lane(float angle, int lane);
void game_init(GameState *g, uint32_t seed);
void game_init_difficulty(GameState *g, uint32_t seed, Difficulty diff);
void game_update(GameState *g, const InputFrame *in, float dt);
/* Silent title-screen game. It keeps playing and never stays on game over. */
void demo_reset(GameState *g, uint32_t seed, Difficulty diff);
void demo_update(GameState *g, float dt);
/* Remaining fraction of this wave. 1 before anything is resolved, 0 when
   every walker, flyer, splitter, and child has been killed or has reached
   the base. Each splitter reserves two children, so a split cannot raise it. */
float starpost_wave_gauge(const GameState *g);
/* Clamp one wall-clock slice and return how many fixed steps to run. */
int game_frame_steps(double *acc, double elapsed);

#endif
