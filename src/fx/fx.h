#ifndef STARPOST_FX_H
#define STARPOST_FX_H

#define FX_MAX 96
#define FX_BURST 8
#define FX_LIFE 0.34f
#define FX_SHAKE_TIME 0.16f
#define FX_SHAKE_MAG 0.018f

typedef struct {
    float x, y;
    float vx, vy;
    float life;
    float r, g, b;
} FxShard;

typedef struct {
    FxShard shards[FX_MAX];
    float shake;
} FxState;

void fx_init(FxState *fx);
void fx_update(FxState *fx, float dt);
void fx_burst(FxState *fx, float x, float y, float r, float g, float b);
void fx_hit(FxState *fx);
void fx_shake_offset(const FxState *fx, float *x, float *y);
int fx_count(const FxState *fx);

#endif
