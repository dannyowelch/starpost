#ifndef STARPOST_RENDER_H
#define STARPOST_RENDER_H

#include "game/game.h"

int render_init(void);
void render_draw(const GameState *g, int width, int height);
int render_screenshot(const char *path, int width, int height);

#endif
