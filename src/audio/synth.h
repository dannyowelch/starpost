#ifndef STARPOST_SYNTH_H
#define STARPOST_SYNTH_H

#include <stdint.h>

#define SYNTH_RATE 44100
#define SYNTH_VOICES 16

typedef struct {
    float phase;
    float freq;
    float slide;
    float amp;
    float decay;
    int wave; /* 0 off, 1 square, 2 saw, 3 noise, 4 sine */
    uint32_t rng;
} SynthVoice;

typedef struct {
    SynthVoice voices[SYNTH_VOICES];
    float gain;
    float warble_amp;
    float warble_phase;
    float warble_lfo;
    int flyers;
    uint32_t noise;
} Synth;

void synth_init(Synth *s, uint32_t seed);
void synth_set_gain(Synth *s, float gain);
void synth_set_flyers(Synth *s, int alive);
void synth_trigger(Synth *s, int game_snd);
void synth_render(Synth *s, float *out, int frames);
int synth_busy(const Synth *s);

#endif
