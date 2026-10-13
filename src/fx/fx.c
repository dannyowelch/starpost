#include "fx.h"

#include <math.h>

void fx_init(FxState *fx)
{
    int i;
    if (!fx)
        return;
    for (i = 0; i < FX_MAX; i++)
        fx->shards[i].life = 0.0f;
    fx->shake = 0.0f;
}

int fx_count(const FxState *fx)
{
    int n = 0;
    int i;
    if (!fx)
        return 0;
    for (i = 0; i < FX_MAX; i++) {
        if (fx->shards[i].life > 0.0f)
            n++;
    }
    return n;
}

static int spawn_slot(FxState *fx)
{
    int i;
    int worst = 0;
    float worst_life = 2.0f;
    for (i = 0; i < FX_MAX; i++) {
        if (fx->shards[i].life <= 0.0f)
            return i;
        if (fx->shards[i].life < worst_life) {
            worst_life = fx->shards[i].life;
            worst = i;
        }
    }
    return worst;
}

void fx_burst(FxState *fx, float x, float y, float r, float g, float b)
{
    int i;
    if (!fx)
        return;
    for (i = 0; i < FX_BURST; i++) {
        FxShard *s = &fx->shards[spawn_slot(fx)];
        float a = ((float)i + 0.5f) * (6.2831853f / (float)FX_BURST);
        s->x = x;
        s->y = y;
        s->vx = cosf(a) * 1.25f;
        s->vy = sinf(a) * 1.25f;
        s->life = 1.0f;
        s->r = r;
        s->g = g;
        s->b = b;
    }
}

void fx_hit(FxState *fx)
{
    if (!fx)
        return;
    fx->shake = FX_SHAKE_TIME;
}

void fx_update(FxState *fx, float dt)
{
    int i;
    if (!fx)
        return;
    if (dt < 0.0f)
        dt = 0.0f;
    if (fx->shake > 0.0f) {
        fx->shake -= dt;
        if (fx->shake < 0.0f)
            fx->shake = 0.0f;
    }
    for (i = 0; i < FX_MAX; i++) {
        FxShard *s = &fx->shards[i];
        if (s->life <= 0.0f)
            continue;
        s->life -= dt / FX_LIFE;
        if (s->life <= 0.0f) {
            s->life = 0.0f;
            continue;
        }
        s->x += s->vx * dt;
        s->y += s->vy * dt;
    }
}

void fx_shake_offset(const FxState *fx, float *x, float *y)
{
    float mag;
    if (!x || !y)
        return;
    *x = 0.0f;
    *y = 0.0f;
    if (!fx || fx->shake <= 0.0f)
        return;
    mag = FX_SHAKE_MAG * (fx->shake / FX_SHAKE_TIME);
    *x = sinf(fx->shake * 95.0f) * mag;
    *y = cosf(fx->shake * 73.0f) * mag * 0.65f;
}
