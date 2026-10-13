#ifndef STARPOST_GAME_H
#define STARPOST_GAME_H

#include <stdbool.h>
#include <stdint.h>

#define STARPOST_LANES 8
#define STARPOST_MAX_ENEMIES 64
#define STARPOST_SHIELD_MAX 8
#define STARPOST_SHIELD_DRAIN 1
#define STARPOST_SHIELD_RECHARGE 2
#define STARPOST_SCORE_KILL 100
#define STARPOST_SCORE_FLYER 300
#define STARPOST_SCORE_WAVE 500
#define STARPOST_FLYER_DRAIN 3
#define STARPOST_ZAP_MAX 4
#define STARPOST_ZAP_FLASH 0.28f
/* Fraction of one lane sector, centered on the tracer. 1 covers the
   whole sector; smaller leaves a gap so a flyer between lanes is missed. */
#define STARPOST_HIT_WINDOW 0.55f
#define STARPOST_FLYER_ANG_SPEED 1.8f
#define STARPOST_FLYER_SPEED_SCALE 0.72f
#define STARPOST_WAVE_CLEAR_TIME 1.5f
#define STARPOST_DT (1.0f / 120.0f)
#define STARPOST_BEAM_TIME 0.18f
#define STARPOST_FIRE_COOLDOWN 0.32f
#define STARPOST_TURN_REPEAT 0.13f
/* A slow frame must not replay an unbounded stretch of simulation. */
#define STARPOST_MAX_FRAME_SEC 0.25
#define STARPOST_MAX_SIM_STEPS 8

/* One table for the energy prototype. Slow's factor applies to walkers only.
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
    float slow_time;
    float slow_factor;
    float deny_time;
    float spread_time;
} EnergyTune;

typedef enum {
    ENEMY_NONE = 0,
    ENEMY_WALKER = 1,
    ENEMY_FLYER = 2
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
} Enemy;

typedef struct {
    uint8_t lane;
    uint8_t beam_lane;
    uint8_t spread_lane;
    float cooldown;
    float beam_timer;
    float spread_timer;
} Player;

typedef struct {
    bool left;
    bool right;
    bool fire;
    bool zap;
    bool spread;
    bool energize;
    bool slow;
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
} DiffTune;

typedef struct {
    int wave;
    int enemy_count;
    int flyer_count;
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
    GAME_SND_DENY
} GameSnd;

#define GAME_SND_MAX 24

typedef struct {
    Enemy enemies[STARPOST_MAX_ENEMIES];
    Player player;
    WaveConfig cfg;
    int spawned;
    int flyers_spawned;
    int killed;
    int arrived;
    float spawn_timer;
    float clear_timer;
    float turn_timer;
    float zap_flash;
    float deny_flash;
    float energize_timer;
    float slow_timer;
    int energy;
    int energize_lane;
    bool prev_left;
    bool prev_right;
    bool prev_zap;
    bool prev_spread;
    bool prev_energize;
    bool prev_slow;
    int shield;
    int shield_max;
    int shield_recharge;
    Difficulty difficulty;
    uint32_t score;
    uint32_t rng;
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
/* Remaining fraction of this wave. 1 before any enemy is resolved, 0 when
   every walker and flyer has been killed or has reached the base. */
float starpost_wave_gauge(const GameState *g);
/* Clamp one wall-clock slice and return how many fixed steps to run. */
int game_frame_steps(double *acc, double elapsed);

#endif
