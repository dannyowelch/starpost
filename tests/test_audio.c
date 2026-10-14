#include "audio/synth.h"
#include "game/game.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

static int heard(const GameState *g, int id)
{
    int i;
    for (i = 0; i < g->snd_count; i++) {
        if (g->snd[i] == (uint8_t)id)
            return 1;
    }
    return 0;
}

static int finite_block(const float *s, int n, float *peak)
{
    int i;
    float p = 0.0f;
    for (i = 0; i < n; i++) {
        float a;
        if (!isfinite(s[i]) || s[i] > 1.0f || s[i] < -1.0f)
            return 0;
        a = s[i] < 0.0f ? -s[i] : s[i];
        if (a > p)
            p = a;
    }
    if (peak)
        *peak = p;
    return 1;
}

static void test_deterministic_and_limits(void)
{
    Synth a, b;
    float *xa;
    float *xb;
    float peak = 0.0f;
    int n = SYNTH_RATE / 2;
    int id;

    xa = calloc((size_t)n, sizeof(float));
    xb = calloc((size_t)n, sizeof(float));
    CHECK(xa && xb);
    synth_init(&a, 7);
    synth_init(&b, 7);
    for (id = GAME_SND_STEP; id <= GAME_SND_OVER; id++) {
        synth_trigger(&a, id);
        synth_trigger(&b, id);
    }
    synth_set_flyers(&a, 1);
    synth_set_flyers(&b, 1);
    synth_render(&a, xa, n);
    synth_render(&b, xb, n);
    CHECK(memcmp(xa, xb, (size_t)n * sizeof(float)) == 0);
    CHECK(finite_block(xa, n, &peak));
    CHECK(peak > 0.02f);
    CHECK(peak <= 1.0f);

    synth_init(&a, 3);
    synth_set_gain(&a, 0.0f);
    synth_trigger(&a, GAME_SND_ZAP);
    synth_render(&a, xa, n);
    CHECK(finite_block(xa, n, &peak));
    CHECK(peak == 0.0f);
    free(xa);
    free(xb);
}

static void test_envelopes_end(void)
{
    Synth s;
    float *buf;
    int n = SYNTH_RATE * 3;
    int id;
    float peak = 0.0f;

    buf = calloc((size_t)n, sizeof(float));
    CHECK(buf != NULL);
    for (id = GAME_SND_STEP; id <= GAME_SND_OVER; id++) {
        synth_init(&s, 11);
        synth_trigger(&s, id);
        synth_render(&s, buf, 200);
        CHECK(synth_busy(&s));
        synth_render(&s, buf, n);
        CHECK(!synth_busy(&s));
        CHECK(finite_block(buf + n - 2000, 2000, &peak));
        CHECK(peak < 0.01f);
    }

    synth_init(&s, 5);
    synth_set_flyers(&s, 1);
    synth_render(&s, buf, SYNTH_RATE / 5);
    CHECK(synth_busy(&s));
    CHECK(s.warble_amp > 0.01f);
    synth_set_flyers(&s, 0);
    synth_render(&s, buf, SYNTH_RATE);
    CHECK(s.warble_amp == 0.0f);
    CHECK(!synth_busy(&s));
    free(buf);
}

static void write_wav(const char *path, const float *s, int n)
{
    FILE *f;
    int i;
    uint32_t data_bytes = (uint32_t)n * 2u;
    uint32_t riff = 36u + data_bytes;
    uint16_t audio_format = 1;
    uint16_t channels = 1;
    uint32_t rate = SYNTH_RATE;
    uint32_t byte_rate = rate * 2u;
    uint16_t block = 2;
    uint16_t bits = 16;
    unsigned char hdr[44];

    f = fopen(path, "wb");
    CHECK(f != NULL);
    if (!f)
        return;
    memcpy(hdr, "RIFF", 4);
    memcpy(hdr + 4, &riff, 4);
    memcpy(hdr + 8, "WAVEfmt ", 8);
    hdr[16] = 16;
    hdr[17] = hdr[18] = hdr[19] = 0;
    memcpy(hdr + 20, &audio_format, 2);
    memcpy(hdr + 22, &channels, 2);
    memcpy(hdr + 24, &rate, 4);
    memcpy(hdr + 28, &byte_rate, 4);
    memcpy(hdr + 32, &block, 2);
    memcpy(hdr + 34, &bits, 2);
    memcpy(hdr + 36, "data", 4);
    memcpy(hdr + 40, &data_bytes, 4);
    CHECK(fwrite(hdr, 1, 44, f) == 44);
    for (i = 0; i < n; i++) {
        float x = s[i];
        int v;
        int16_t pcm;
        if (x > 1.0f)
            x = 1.0f;
        if (x < -1.0f)
            x = -1.0f;
        v = (int)(x * 32767.0f);
        pcm = (int16_t)v;
        if (fwrite(&pcm, 2, 1, f) != 1) {
            CHECK(0);
            break;
        }
    }
    fclose(f);
}

static void render_one(const char *path, int id, int flyers, int frames)
{
    Synth s;
    float *buf = calloc((size_t)frames, sizeof(float));
    float peak = 0.0f;
    CHECK(buf != NULL);
    if (!buf)
        return;
    synth_init(&s, 99);
    if (id)
        synth_trigger(&s, id);
    synth_set_flyers(&s, flyers);
    synth_render(&s, buf, frames);
    CHECK(finite_block(buf, frames, &peak));
    CHECK(peak > 0.01f);
    write_wav(path, buf, frames);
    free(buf);
}

static void test_events(void)
{
    GameState g;
    InputFrame in;

    game_init(&g, 1);
    memset(&in, 0, sizeof in);
    in.right = true;
    game_update(&g, &in, STARPOST_DT);
    CHECK(heard(&g, GAME_SND_STEP));
    CHECK(g.player.lane == 1);

    memset(&in, 0, sizeof in);
    in.fire = true;
    g.spawn_timer = 1000.0f;
    game_update(&g, &in, STARPOST_DT);
    CHECK(heard(&g, GAME_SND_FIRE));
    CHECK(!heard(&g, GAME_SND_WALKER_KILL));

    g.spawn_timer = 1000.0f;
    g.player.cooldown = 0.0f;
    g.enemies[0].kind = ENEMY_WALKER;
    g.enemies[0].alive = true;
    g.enemies[0].lane = g.player.lane;
    g.enemies[0].dist = 0.4f;
    g.enemies[0].speed = 0.0f;
    memset(g.shots, 0, sizeof g.shots);
    g.player.cooldown = 0.0f;
    game_update(&g, &in, 0.4f / STARPOST_SHOT_SPEED + 0.004f);
    CHECK(heard(&g, GAME_SND_WALKER_KILL));

    g.spawn_timer = 1000.0f;
    g.player.cooldown = 0.0f;
    memset(g.shots, 0, sizeof g.shots);
    g.enemies[1].kind = ENEMY_FLYER;
    g.enemies[1].alive = true;
    g.enemies[1].dist = 0.5f;
    g.enemies[1].speed = 0.0f;
    g.enemies[1].ang_speed = 0.0f;
    g.enemies[1].angle = starpost_lane_center(g.player.lane);
    game_update(&g, &in, 0.5f / STARPOST_SHOT_SPEED + 0.004f);
    CHECK(heard(&g, GAME_SND_FLYER_KILL));

    g.cfg.enemy_count = 0;
    g.cfg.flyer_count = 1;
    g.spawned = 0;
    g.flyers_spawned = 0;
    g.spawn_timer = 0.0f;
    memset(&in, 0, sizeof in);
    game_update(&g, &in, STARPOST_DT);
    CHECK(heard(&g, GAME_SND_FLYER_SPAWN));

    game_init(&g, 2);
    g.spawn_timer = 1000.0f;
    g.enemies[0].kind = ENEMY_WALKER;
    g.enemies[0].alive = true;
    g.enemies[0].dist = 0.01f;
    g.enemies[0].speed = 1.0f;
    g.enemies[0].lane = 1;
    memset(&in, 0, sizeof in);
    game_update(&g, &in, 0.05f);
    CHECK(heard(&g, GAME_SND_BASE));
    CHECK(!heard(&g, GAME_SND_OVER));

    g.shield = 1;
    g.enemies[0].kind = ENEMY_WALKER;
    g.enemies[0].alive = true;
    g.enemies[0].dist = 0.01f;
    g.enemies[0].speed = 1.0f;
    game_update(&g, &in, 0.05f);
    CHECK(heard(&g, GAME_SND_OVER));

    game_init(&g, 3);
    g.spawn_timer = 1000.0f;
    g.spawned = g.cfg.enemy_count + g.cfg.flyer_count;
    memset(&in, 0, sizeof in);
    game_update(&g, &in, STARPOST_DT);
    CHECK(g.state == STATE_WAVE_CLEAR);
    CHECK(heard(&g, GAME_SND_WAVE));

    game_init(&g, 4);
    g.spawn_timer = 1000.0f;
    g.energy = energy_tune()->zap_cost;
    g.enemies[0].alive = true;
    g.enemies[0].kind = ENEMY_WALKER;
    g.enemies[0].dist = 0.4f;
    g.enemies[0].speed = 0.0f;
    memset(&in, 0, sizeof in);
    in.zap = true;
    game_update(&g, &in, STARPOST_DT);
    CHECK(heard(&g, GAME_SND_ZAP));
    CHECK(heard(&g, GAME_SND_WALKER_KILL));

    game_init(&g, 5);
    g.spawn_timer = 1000.0f;
    g.energy = 0;
    g.enemies[0].alive = true;
    g.enemies[0].kind = ENEMY_WALKER;
    g.enemies[0].dist = 0.4f;
    memset(&in, 0, sizeof in);
    in.zap = true;
    game_update(&g, &in, STARPOST_DT);
    CHECK(!heard(&g, GAME_SND_ZAP));
    CHECK(g.enemies[0].alive);
}

int main(void)
{
    test_deterministic_and_limits();
    test_envelopes_end();
    test_events();
    render_one("/opt/cursor/artifacts/sfx-step.wav", GAME_SND_STEP, 0, SYNTH_RATE / 8);
    render_one("/opt/cursor/artifacts/sfx-fire.wav", GAME_SND_FIRE, 0, SYNTH_RATE / 4);
    render_one("/opt/cursor/artifacts/sfx-walker-kill.wav", GAME_SND_WALKER_KILL, 0, SYNTH_RATE / 4);
    render_one("/opt/cursor/artifacts/sfx-flyer-spawn.wav", GAME_SND_FLYER_SPAWN, 0, SYNTH_RATE / 3);
    render_one("/opt/cursor/artifacts/sfx-flyer-kill.wav", GAME_SND_FLYER_KILL, 0, SYNTH_RATE / 3);
    render_one("/opt/cursor/artifacts/sfx-flyer-warble.wav", 0, 1, SYNTH_RATE);
    render_one("/opt/cursor/artifacts/sfx-base.wav", GAME_SND_BASE, 0, SYNTH_RATE / 3);
    render_one("/opt/cursor/artifacts/sfx-superzap.wav", GAME_SND_ZAP, 0, SYNTH_RATE / 2);
    render_one("/opt/cursor/artifacts/sfx-wave.wav", GAME_SND_WAVE, 0, SYNTH_RATE / 2);
    render_one("/opt/cursor/artifacts/sfx-gameover.wav", GAME_SND_OVER, 0, SYNTH_RATE / 2);
    render_one("/opt/cursor/artifacts/sfx-spread.wav", GAME_SND_SPREAD, 0, SYNTH_RATE / 4);
    render_one("/opt/cursor/artifacts/sfx-energize.wav", GAME_SND_ENERGIZE, 0, SYNTH_RATE / 3);
    render_one("/opt/cursor/artifacts/sfx-slow.wav", GAME_SND_SLOW, 0, SYNTH_RATE / 3);
    render_one("/opt/cursor/artifacts/sfx-deny.wav", GAME_SND_DENY, 0, SYNTH_RATE / 8);
    render_one("/opt/cursor/artifacts/sfx-repair.wav", GAME_SND_REPAIR, 0, SYNTH_RATE / 4);
    render_one("/opt/cursor/artifacts/sfx-split.wav", GAME_SND_SPLIT, 0, SYNTH_RATE / 4);
    render_one("/opt/cursor/artifacts/sfx-split-kill.wav", GAME_SND_SPLIT_KILL, 0, SYNTH_RATE / 8);
    if (fails) {
        fprintf(stderr, "%d/%d audio checks failed\n", fails, checks);
        return 1;
    }
    printf("%d audio checks passed\n", checks);
    return 0;
}
