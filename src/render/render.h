#ifndef STARPOST_RENDER_H
#define STARPOST_RENDER_H

#include "game/game.h"

struct Session;

int render_init(void);
void render_session(const struct Session *s, int width, int height, float dt);
void render_draw(const GameState *g, int width, int height, float dt);
void render_redraw(const GameState *g, int width, int height);
void render_set_bloom(int enabled);
int render_bloom_enabled(void);
int render_shard_count(void);
int render_screenshot(const char *path, int width, int height);

#endif
