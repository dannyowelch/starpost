#include "fx/fx.h"

#include <stdio.h>

static int fails;
static int checks;

static void check(int cond, const char *expr, const char *file, int line)
{
    checks++;
    if (!cond) {
        fprintf(stderr, "FAIL %s:%d: %s\n", file, line, expr);
        fails++;
    }
}

#define CHECK(cond) check((cond), #cond, __FILE__, __LINE__)

static void test_burst_moves_and_fades(void)
{
    FxState a, b;
    int i;

    fx_init(&a);
    CHECK(fx_count(&a) == 0);
    CHECK(a.shake == 0.0f);

    fx_burst(&a, 0.0f, 0.0f, 1.0f, 0.2f, 0.8f);
    CHECK(fx_count(&a) == FX_BURST);
    for (i = 0; i < FX_BURST; i++) {
        CHECK(a.shards[i].life == 1.0f);
        CHECK(a.shards[i].x == 0.0f);
        CHECK(a.shards[i].y == 0.0f);
    }

    fx_init(&b);
    fx_burst(&b, 0.0f, 0.0f, 1.0f, 0.2f, 0.8f);
    fx_update(&a, 0.05f);
    fx_update(&b, 0.05f);
    for (i = 0; i < FX_BURST; i++) {
        float outward = a.shards[i].x * a.shards[i].vx + a.shards[i].y * a.shards[i].vy;
        CHECK(a.shards[i].x == b.shards[i].x);
        CHECK(a.shards[i].y == b.shards[i].y);
        CHECK(a.shards[i].life == b.shards[i].life);
        CHECK(a.shards[i].life < 1.0f);
        CHECK(a.shards[i].life > 0.0f);
        CHECK(outward > 0.0f);
    }

    fx_update(&a, 10.0f);
    CHECK(fx_count(&a) == 0);
}

static void test_burst_cap(void)
{
    FxState fx;
    int i;
    int fresh = 0;

    fx_init(&fx);
    for (i = 0; i < 20; i++)
        fx_burst(&fx, 0.2f, -0.3f, 1.0f, 0.1f, 0.4f);
    CHECK(fx_count(&fx) == FX_MAX);
    fx_update(&fx, 0.05f);
    CHECK(fx_count(&fx) == FX_MAX);
    fx_burst(&fx, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f);
    CHECK(fx_count(&fx) <= FX_MAX);
    for (i = 0; i < FX_MAX; i++) {
        if (fx.shards[i].life == 1.0f)
            fresh++;
    }
    CHECK(fresh == FX_BURST);
}

static void test_shake(void)
{
    FxState fx;
    float x, y;

    fx_init(&fx);
    fx_burst(&fx, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f);
    fx_hit(&fx);
    CHECK(fx.shake == FX_SHAKE_TIME);
    CHECK(fx_count(&fx) == FX_BURST);
    fx_shake_offset(&fx, &x, &y);
    CHECK(x != 0.0f || y != 0.0f);

    fx_update(&fx, 0.05f);
    CHECK(fx.shake == FX_SHAKE_TIME - 0.05f);
    CHECK(fx_count(&fx) == FX_BURST);

    fx_update(&fx, FX_SHAKE_TIME);
    CHECK(fx.shake == 0.0f);
    fx_shake_offset(&fx, &x, &y);
    CHECK(x == 0.0f);
    CHECK(y == 0.0f);
}

int main(void)
{
    test_burst_moves_and_fades();
    test_burst_cap();
    test_shake();
    if (fails) {
        fprintf(stderr, "%d/%d fx checks failed\n", fails, checks);
        return 1;
    }
    printf("%d fx checks passed\n", checks);
    return 0;
}
