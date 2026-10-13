#ifndef STARPOST_META_H
#define STARPOST_META_H

#include "game/game.h"

#include <stdbool.h>
#include <stdint.h>

#define SCORE_CAP 10
#define SCORE_INITIALS 3

typedef enum {
    SCREEN_TITLE = 0,
    SCREEN_DIFFICULTY,
    SCREEN_SCORES,
    SCREEN_PLAY,
    SCREEN_PAUSE,
    SCREEN_INITIALS
} Screen;

typedef enum {
    TITLE_START = 0,
    TITLE_DIFFICULTY,
    TITLE_SCORES,
    TITLE_QUIT,
    TITLE_COUNT
} TitleItem;

typedef enum {
    PAUSE_RESUME = 0,
    PAUSE_QUIT,
    PAUSE_COUNT
} PauseItem;

typedef struct {
    char initials[SCORE_INITIALS + 1];
    uint32_t score;
} ScoreRow;

typedef struct {
    ScoreRow rows[DIFF_COUNT][SCORE_CAP];
    int count[DIFF_COUNT];
} ScoreTable;

typedef struct SessionInput {
    InputFrame play;
    bool up;
    bool down;
    bool accept;
    bool pause;
} SessionInput;

typedef struct Session {
    Screen screen;
    int title_item;
    int pause_item;
    int diff_item;
    int score_diff;
    Difficulty difficulty;
    GameState game;
    ScoreTable scores;
    char initials[SCORE_INITIALS];
    int initial_slot;
    uint32_t pending_score;
    uint32_t seed;
    int quit;
    int scores_dirty;
    float attract;
    bool prev_up;
    bool prev_down;
    bool prev_left;
    bool prev_right;
    bool prev_accept;
    bool prev_pause;
} Session;

void scores_clear(ScoreTable *t);
int scores_qualifies(const ScoreTable *t, Difficulty diff, uint32_t score);
void scores_insert(ScoreTable *t, Difficulty diff, const char initials[SCORE_INITIALS],
                   uint32_t score);
int scores_load(ScoreTable *t, const char *path);
int scores_save(const ScoreTable *t, const char *path);
int scores_default_path(char *buf, int cap);

void session_init(Session *s, uint32_t seed);
/* Returns 1 when this call stepped the simulation. Pause and menus do not. */
int session_update(Session *s, const SessionInput *in, float dt);

#endif
