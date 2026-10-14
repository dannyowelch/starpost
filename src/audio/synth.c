#include "synth.h"

#include "game/game.h"

#include <math.h>

static float mul_for(float seconds)
{
    if (seconds < 0.005f)
        seconds = 0.005f;
    return expf(-1.0f / ((float)SYNTH_RATE * seconds));
}

static void voice_on(Synth *s, int wave, float freq, float slide, float amp, float seconds)
{
    int i;
    int slot = 0;
    float quiet = 1.0e9f;
    for (i = 0; i < SYNTH_VOICES; i++) {
        if (s->voices[i].wave == 0) {
            slot = i;
            break;
        }
        if (s->voices[i].amp < quiet) {
            quiet = s->voices[i].amp;
            slot = i;
        }
    }
    s->voices[slot].phase = 0.0f;
    s->voices[slot].freq = freq;
    s->voices[slot].slide = slide;
    s->voices[slot].amp = amp;
    s->voices[slot].decay = mul_for(seconds);
    s->voices[slot].wave = wave;
    s->voices[slot].rng = s->noise;
    s->noise = s->noise * 1664525u + 1013904223u;
    if (s->noise == 0)
        s->noise = 1;
}

void synth_init(Synth *s, uint32_t seed)
{
    int i;
    if (!s)
        return;
    for (i = 0; i < SYNTH_VOICES; i++)
        s->voices[i].wave = 0;
    s->gain = 0.55f;
    s->warble_amp = 0.0f;
    s->warble_phase = 0.0f;
    s->warble_lfo = 0.0f;
    s->flyers = 0;
    s->noise = seed ? seed : 1u;
}

void synth_set_gain(Synth *s, float gain)
{
    if (!s)
        return;
    if (gain < 0.0f)
        gain = 0.0f;
    if (gain > 1.0f)
        gain = 1.0f;
    s->gain = gain;
}

void synth_set_flyers(Synth *s, int alive)
{
    if (!s)
        return;
    s->flyers = alive > 0 ? 1 : 0;
}

void synth_trigger(Synth *s, int game_snd)
{
    if (!s)
        return;
    switch (game_snd) {
    case GAME_SND_STEP:
        voice_on(s, 1, 1760.0f, 0.0f, 0.08f, 0.025f);
        break;
    case GAME_SND_FIRE:
        voice_on(s, 3, 0.0f, 0.0f, 0.12f, 0.05f);
        voice_on(s, 1, 880.0f, -2400.0f, 0.14f, 0.08f);
        break;
    case GAME_SND_WALKER_KILL:
        voice_on(s, 1, 620.0f, -1800.0f, 0.12f, 0.06f);
        break;
    case GAME_SND_FLYER_SPAWN:
        voice_on(s, 2, 240.0f, 2200.0f, 0.13f, 0.14f);
        break;
    case GAME_SND_FLYER_KILL:
        voice_on(s, 2, 740.0f, -1600.0f, 0.14f, 0.10f);
        voice_on(s, 1, 1480.0f, -900.0f, 0.08f, 0.08f);
        break;
    case GAME_SND_BASE:
        voice_on(s, 3, 0.0f, 0.0f, 0.16f, 0.09f);
        voice_on(s, 1, 110.0f, -80.0f, 0.16f, 0.16f);
        break;
    case GAME_SND_ZAP:
        voice_on(s, 3, 0.0f, 0.0f, 0.20f, 0.22f);
        voice_on(s, 2, 90.0f, 1400.0f, 0.18f, 0.28f);
        voice_on(s, 1, 220.0f, 600.0f, 0.10f, 0.20f);
        break;
    case GAME_SND_WAVE:
        voice_on(s, 4, 523.0f, 900.0f, 0.12f, 0.28f);
        break;
    case GAME_SND_OVER:
        voice_on(s, 2, 280.0f, -420.0f, 0.16f, 0.45f);
        break;
    case GAME_SND_SPREAD:
        voice_on(s, 3, 0.0f, 0.0f, 0.14f, 0.07f);
        voice_on(s, 1, 1320.0f, -2800.0f, 0.16f, 0.10f);
        break;
    case GAME_SND_ENERGIZE:
        voice_on(s, 4, 440.0f, 1600.0f, 0.14f, 0.22f);
        voice_on(s, 1, 880.0f, 400.0f, 0.08f, 0.16f);
        break;
    case GAME_SND_SLOW:
        voice_on(s, 2, 180.0f, -220.0f, 0.16f, 0.32f);
        voice_on(s, 4, 330.0f, -80.0f, 0.08f, 0.28f);
        break;
    case GAME_SND_DENY:
        voice_on(s, 1, 140.0f, -40.0f, 0.10f, 0.07f);
        break;
    case GAME_SND_REPAIR:
        voice_on(s, 4, 660.0f, 480.0f, 0.12f, 0.12f);
        voice_on(s, 1, 1320.0f, 200.0f, 0.06f, 0.08f);
        break;
    case GAME_SND_SPLIT:
        voice_on(s, 3, 0.0f, 0.0f, 0.16f, 0.08f);
        voice_on(s, 1, 520.0f, 1100.0f, 0.12f, 0.11f);
        break;
    case GAME_SND_SPLIT_KILL:
        voice_on(s, 1, 980.0f, -1600.0f, 0.10f, 0.05f);
        break;
    default:
        break;
    }
}

static float voice_sample(SynthVoice *v)
{
    float x;
    uint32_t n;
    if (v->wave == 0 || v->amp <= 0.0f)
        return 0.0f;
    v->phase += v->freq / (float)SYNTH_RATE;
    if (v->phase >= 1.0f)
        v->phase -= (float)(int)v->phase;
    v->freq += v->slide / (float)SYNTH_RATE;
    if (v->freq < 30.0f)
        v->freq = 30.0f;
    if (v->freq > 6000.0f)
        v->freq = 6000.0f;
    if (v->wave == 1)
        x = (v->phase < 0.5f) ? 1.0f : -1.0f;
    else if (v->wave == 2)
        x = v->phase * 2.0f - 1.0f;
    else if (v->wave == 4)
        x = sinf(v->phase * 6.2831853f);
    else {
        n = v->rng;
        n ^= n << 13;
        n ^= n >> 17;
        n ^= n << 5;
        if (n == 0)
            n = 1;
        v->rng = n;
        x = ((int)(n & 65535) - 32768) / 32768.0f;
    }
    x *= v->amp;
    v->amp *= v->decay;
    if (v->amp < 0.0008f) {
        v->amp = 0.0f;
        v->wave = 0;
    }
    return x;
}

void synth_render(Synth *s, float *out, int frames)
{
    int i, n;
    if (!s || !out || frames <= 0)
        return;
    for (n = 0; n < frames; n++) {
        float mix = 0.0f;
        float wobble;
        for (i = 0; i < SYNTH_VOICES; i++)
            mix += voice_sample(&s->voices[i]);
        if (s->flyers) {
            s->warble_amp += (0.055f - s->warble_amp) * 0.01f;
        } else {
            s->warble_amp *= 0.9992f;
            if (s->warble_amp < 0.0008f)
                s->warble_amp = 0.0f;
        }
        if (s->warble_amp > 0.0f) {
            s->warble_lfo += 7.0f / (float)SYNTH_RATE;
            if (s->warble_lfo >= 1.0f)
                s->warble_lfo -= 1.0f;
            wobble = 1.0f + 0.14f * sinf(s->warble_lfo * 6.2831853f);
            s->warble_phase += (196.0f * wobble) / (float)SYNTH_RATE;
            if (s->warble_phase >= 1.0f)
                s->warble_phase -= 1.0f;
            mix += sinf(s->warble_phase * 6.2831853f) * s->warble_amp;
        }
        mix *= s->gain;
        mix = tanhf(mix);
        out[n] = mix;
    }
}

int synth_busy(const Synth *s)
{
    int i;
    if (!s)
        return 0;
    if (s->warble_amp > 0.0f)
        return 1;
    for (i = 0; i < SYNTH_VOICES; i++) {
        if (s->voices[i].wave != 0)
            return 1;
    }
    return 0;
}
