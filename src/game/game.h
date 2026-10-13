#ifndef STARPOST_GAME_H
#define STARPOST_GAME_H

#include <stdbool.h>
#include <stdint.h>

#define STARPOST_LANES 8
#define STARPOST_MAX_ENEMIES 64
#define STARPOST_SHIELD_MAX 8
#define STARPOST_SHIELD_DRAIN 1
#define STARPOST_SHIELD_RECHARGE 2
#define STARPOST_DT (1.0f / 120.0f)
#define STARPOST_BEAM_TIME 0.18f
#define STARPOST_FIRE_COOLDOWN 0.16f
#define STARPOST_TURN_REPEAT 0.13f

typedef enum {
    ENEMY_NONE = 0,
    ENEMY_WALKER = 1
} EnemyKind;

typedef enum {
    STATE_PLAYING = 0,
    STATE_GAME_OVER = 1
} GamePlayState;

typedef struct {
    EnemyKind kind;
    uint8_t lane;
    float dist;
    float speed;
    bool alive;
} Enemy;

typedef struct {
    uint8_t lane;
    uint8_t beam_lane;
    float cooldown;
    float beam_timer;
} Player;

typedef struct {
    bool left;
    bool right;
    bool fire;
    bool confirm;
} InputFrame;

typedef struct {
    Enemy enemies[STARPOST_MAX_ENEMIES];
    Player player;
    int shield;
    int shield_max;
    float spawn_timer;
    float turn_timer;
    bool prev_left;
    bool prev_right;
    uint32_t rng;
    GamePlayState state;
} GameState;

void game_init(GameState *g, uint32_t seed);
void game_update(GameState *g, const InputFrame *in, float dt);

#endif
