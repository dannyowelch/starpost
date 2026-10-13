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
#define STARPOST_FIRE_COOLDOWN 0.16f
#define STARPOST_TURN_REPEAT 0.13f

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
    uint8_t zaps;
    float cooldown;
    float beam_timer;
} Player;

typedef struct {
    bool left;
    bool right;
    bool fire;
    bool zap;
    bool confirm;
} InputFrame;

typedef struct {
    int wave;
    int enemy_count;
    int flyer_count;
    float spawn_interval;
    float enemy_speed;
} WaveConfig;

typedef struct {
    Enemy enemies[STARPOST_MAX_ENEMIES];
    Player player;
    WaveConfig cfg;
    int spawned;
    int flyers_spawned;
    int killed;
    float spawn_timer;
    float clear_timer;
    float turn_timer;
    float zap_flash;
    bool prev_left;
    bool prev_right;
    bool prev_zap;
    int shield;
    int shield_max;
    uint32_t score;
    uint32_t rng;
    GamePlayState state;
} GameState;

WaveConfig wave_config(int wave);
float starpost_lane_center(int lane);
int starpost_lane_from_angle(float angle);
int starpost_flyer_in_lane(float angle, int lane);
void game_init(GameState *g, uint32_t seed);
void game_update(GameState *g, const InputFrame *in, float dt);

#endif
