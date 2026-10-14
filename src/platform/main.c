#include "audio/synth.h"
#include "game/game.h"
#include "input/pad.h"
#include "meta/meta.h"
#include "render/render.h"

#include <SDL3/SDL.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(void)
{
    fprintf(stderr,
            "usage: starpost [--frames N] [--screenshot PATH]\n"
            "                 [--nobloom-screenshot PATH] [--burst-screenshot PATH]\n"
            "                 [--flyer-screenshot PATH] [--zap-screenshot PATH]\n"
            "  Left/Right or A/D  rotate one lane\n"
            "  Space or J         fire (1 energy)\n"
            "  Z or Shift         superzap (20 energy)\n"
            "  X                  spread shot (2 energy)\n"
            "  Q                  energize the aimed lane (5 energy)\n"
            "  E                  slow lane enemies to 30%% speed (10 energy, 5 s)\n"
            "  R                  repair one shield pip (10 energy)\n"
            "  M                  mute\n"
            "  - / =              volume\n"
            "  F2                 toggle bloom\n"
            "  Up/Down            move through menus and initials\n"
            "  Enter or Space     accept a menu item or initials\n"
            "  Esc or P           pause during play, resume, or leave a menu\n"
            "  --soak-pad SEC     run SEC seconds with a virtual gamepad\n"
            "  --title-screenshot PATH\n"
            "  --pause-screenshot PATH\n"
            "  --initials-screenshot PATH\n"
            "  --scores-screenshot PATH\n"
            "  --splitter-screenshot PATH\n"
            "  --split-screenshot PATH\n"
            "  --flight-screenshot PATH\n"
            "  --volley-screenshot PATH\n"
            "  STARPOST_LOG_GAMEPAD=1  log pad identity, event counts, slow polls\n");
}

static int parse_args(int argc, char **argv, int *frames, const char **shot,
                      const char **nobloom, const char **burst, const char **flyer,
                      const char **zapshot, double *soak, const char **title_shot,
                      const char **pause_shot, const char **initials_shot,
                      const char **scores_shot, const char **spread_shot,
                      const char **energize_shot, const char **slow_shot,
                      const char **splitter_shot, const char **split_shot,
                      const char **flight_shot, const char **volley_shot)
{
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            *frames = atoi(argv[++i]);
            if (*frames <= 0) {
                fprintf(stderr, "--frames needs a positive count\n");
                return -1;
            }
        } else if (strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc) {
            *shot = argv[++i];
        } else if (strcmp(argv[i], "--nobloom-screenshot") == 0 && i + 1 < argc) {
            *nobloom = argv[++i];
        } else if (strcmp(argv[i], "--burst-screenshot") == 0 && i + 1 < argc) {
            *burst = argv[++i];
        } else if (strcmp(argv[i], "--flyer-screenshot") == 0 && i + 1 < argc) {
            *flyer = argv[++i];
        } else if (strcmp(argv[i], "--zap-screenshot") == 0 && i + 1 < argc) {
            *zapshot = argv[++i];
        } else if (strcmp(argv[i], "--soak-pad") == 0 && i + 1 < argc) {
            *soak = atof(argv[++i]);
            if (*soak <= 0.0) {
                fprintf(stderr, "--soak-pad needs a positive number of seconds\n");
                return -1;
            }
        } else if (strcmp(argv[i], "--title-screenshot") == 0 && i + 1 < argc) {
            *title_shot = argv[++i];
        } else if (strcmp(argv[i], "--pause-screenshot") == 0 && i + 1 < argc) {
            *pause_shot = argv[++i];
        } else if (strcmp(argv[i], "--initials-screenshot") == 0 && i + 1 < argc) {
            *initials_shot = argv[++i];
        } else if (strcmp(argv[i], "--scores-screenshot") == 0 && i + 1 < argc) {
            *scores_shot = argv[++i];
        } else if (strcmp(argv[i], "--spread-screenshot") == 0 && i + 1 < argc) {
            *spread_shot = argv[++i];
        } else if (strcmp(argv[i], "--energize-screenshot") == 0 && i + 1 < argc) {
            *energize_shot = argv[++i];
        } else if (strcmp(argv[i], "--slow-screenshot") == 0 && i + 1 < argc) {
            *slow_shot = argv[++i];
        } else if (strcmp(argv[i], "--splitter-screenshot") == 0 && i + 1 < argc) {
            *splitter_shot = argv[++i];
        } else if (strcmp(argv[i], "--split-screenshot") == 0 && i + 1 < argc) {
            *split_shot = argv[++i];
        } else if (strcmp(argv[i], "--flight-screenshot") == 0 && i + 1 < argc) {
            *flight_shot = argv[++i];
        } else if (strcmp(argv[i], "--volley-screenshot") == 0 && i + 1 < argc) {
            *volley_shot = argv[++i];
        } else if (strcmp(argv[i], "--help") == 0) {
            usage();
            return 1;
        } else {
            usage();
            return -1;
        }
    }
    return 0;
}

static Synth synth;
static SDL_AudioStream *audio_stream;
static SDL_Gamepad *pad;
static float volume = 0.55f;
static int muted;
static int log_pad;
static int stick_hold_left;
static int stick_hold_right;
static Uint64 pad_reopen_at;
static SDL_Joystick *virtual_joy;
static int soak_fail;
static double soak_worst_poll_ms;

enum {
    PADLOG_AXIS = 0,
    PADLOG_BUTTON,
    PADLOG_SENSOR,
    PADLOG_TOUCH,
    PADLOG_BATTERY,
    PADLOG_ADDED,
    PADLOG_REMOVED,
    PADLOG_OTHER,
    PADLOG_N
};
static int padlog_count[PADLOG_N];
static Uint64 padlog_next;

static void configure_gamepad(void)
{
    const char *env = getenv("STARPOST_LOG_GAMEPAD");
    /* SDL's HIDAPI drivers for this pad send output reports with a
       blocking hidraw write() on the thread that calls SDL_PollEvent.
       The SF30 Pro ignores several of those reports (SDL's Switch
       driver says so for the USB high-speed command). Enhanced reports
       default on, so a wired Switch-mode pad that is not streaming gets
       a synchronous ForceUSB write every poll, and a Bluetooth pad that
       goes quiet for 3s is closed and reopened through the same setup.
       The 8BitDo and Xbox HIDAPI drivers do the same class of feature
       and LED writes, and they enumerate a second node beside evdev.
       The kernel gamepad node is already in SDL's mapping database.
       An environment variable still wins: SDL_SetHint does not override one. */
    static const char *const hints[][2] = {
        {SDL_HINT_JOYSTICK_HIDAPI_SWITCH, "0"},
        {SDL_HINT_JOYSTICK_HIDAPI_8BITDO, "0"},
        {SDL_HINT_JOYSTICK_HIDAPI_XBOX, "0"},
        {SDL_HINT_JOYSTICK_ENHANCED_REPORTS, "0"},
        {SDL_HINT_JOYSTICK_HIDAPI_SWITCH_PLAYER_LED, "0"},
        {SDL_HINT_JOYSTICK_HIDAPI_XBOX_360_PLAYER_LED, "0"},
    };
    int i;

    log_pad = env && env[0] != '\0' && strcmp(env, "0") != 0;
    for (i = 0; i < (int)(sizeof hints / sizeof hints[0]); i++) {
        if (!SDL_SetHint(hints[i][0], hints[i][1]) && log_pad)
            fprintf(stderr, "gamepad: hint %s left as set in the environment\n", hints[i][0]);
    }
}

static void pad_log_note(Uint32 type)
{
    int slot = PADLOG_OTHER;
    if (!log_pad)
        return;
    switch (type) {
    case SDL_EVENT_GAMEPAD_AXIS_MOTION:
    case SDL_EVENT_JOYSTICK_AXIS_MOTION:
        slot = PADLOG_AXIS;
        break;
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
    case SDL_EVENT_GAMEPAD_BUTTON_UP:
    case SDL_EVENT_JOYSTICK_BUTTON_DOWN:
    case SDL_EVENT_JOYSTICK_BUTTON_UP:
    case SDL_EVENT_JOYSTICK_HAT_MOTION:
        slot = PADLOG_BUTTON;
        break;
    case SDL_EVENT_GAMEPAD_SENSOR_UPDATE:
    case SDL_EVENT_SENSOR_UPDATE:
        slot = PADLOG_SENSOR;
        break;
    case SDL_EVENT_GAMEPAD_TOUCHPAD_DOWN:
    case SDL_EVENT_GAMEPAD_TOUCHPAD_MOTION:
    case SDL_EVENT_GAMEPAD_TOUCHPAD_UP:
        slot = PADLOG_TOUCH;
        break;
    case SDL_EVENT_JOYSTICK_BATTERY_UPDATED:
        slot = PADLOG_BATTERY;
        break;
    case SDL_EVENT_GAMEPAD_ADDED:
    case SDL_EVENT_JOYSTICK_ADDED:
        slot = PADLOG_ADDED;
        break;
    case SDL_EVENT_GAMEPAD_REMOVED:
    case SDL_EVENT_JOYSTICK_REMOVED:
        slot = PADLOG_REMOVED;
        break;
    default:
        break;
    }
    padlog_count[slot]++;
}

static void pad_log_flush(int force)
{
    Uint64 now;
    if (!log_pad)
        return;
    now = SDL_GetTicks();
    if (!force && now < padlog_next)
        return;
    padlog_next = now + 1000;
    fprintf(stderr,
            "gamepad: axis %d button %d sensor %d touch %d battery %d added %d removed %d other %d\n",
            padlog_count[PADLOG_AXIS], padlog_count[PADLOG_BUTTON], padlog_count[PADLOG_SENSOR],
            padlog_count[PADLOG_TOUCH], padlog_count[PADLOG_BATTERY], padlog_count[PADLOG_ADDED],
            padlog_count[PADLOG_REMOVED], padlog_count[PADLOG_OTHER]);
    memset(padlog_count, 0, sizeof padlog_count);
}

static void pad_log_identity(const char *what, SDL_JoystickID id)
{
    if (!log_pad)
        return;
    fprintf(stderr, "gamepad: %s id %u \"%s\" vid %04x pid %04x type %d\n", what,
            (unsigned)id, SDL_GetGamepadNameForID(id) ? SDL_GetGamepadNameForID(id) : "?",
            (unsigned)SDL_GetGamepadVendorForID(id), (unsigned)SDL_GetGamepadProductForID(id),
            (int)SDL_GetGamepadTypeForID(id));
}

static void pad_log_devices(void)
{
    int n = 0;
    int i;
    SDL_JoystickID *ids;
    if (!log_pad)
        return;
    ids = SDL_GetGamepads(&n);
    fprintf(stderr, "gamepad: %d enumerated\n", n);
    for (i = 0; i < n; i++) {
        int j;
        pad_log_identity("found", ids[i]);
        for (j = 0; j < i; j++) {
            if (SDL_GetGamepadVendorForID(ids[i]) == SDL_GetGamepadVendorForID(ids[j]) &&
                SDL_GetGamepadProductForID(ids[i]) == SDL_GetGamepadProductForID(ids[j]))
                fprintf(stderr, "gamepad: duplicate vid/pid %04x:%04x ids %u and %u\n",
                        (unsigned)SDL_GetGamepadVendorForID(ids[i]),
                        (unsigned)SDL_GetGamepadProductForID(ids[i]), (unsigned)ids[j],
                        (unsigned)ids[i]);
        }
    }
    SDL_free(ids);
}

static void pad_try_open(SDL_JoystickID id)
{
    Uint64 now = SDL_GetTicks();
    if (pad)
        return;
    if (now < pad_reopen_at)
        return;
    pad = SDL_OpenGamepad(id);
    /* A flapping connection must not re-run device setup every frame. */
    pad_reopen_at = now + 1000;
    if (pad)
        pad_log_identity("open", id);
    else if (log_pad)
        fprintf(stderr, "gamepad: open id %u failed: %s\n", (unsigned)id, SDL_GetError());
}

static void pad_try_close(SDL_JoystickID id)
{
    if (!pad || SDL_GetGamepadID(pad) != id)
        return;
    pad_log_identity("close", id);
    SDL_CloseGamepad(pad);
    pad = NULL;
    virtual_joy = NULL;
    stick_hold_left = 0;
    stick_hold_right = 0;
}

static int attach_soak_pad(void)
{
    SDL_VirtualJoystickDesc desc;
    static const SDL_VirtualJoystickSensorDesc sensors[2] = {
        {SDL_SENSOR_GYRO, 200.0f},
        {SDL_SENSOR_ACCEL, 200.0f},
    };
    SDL_JoystickID id;

    SDL_INIT_INTERFACE(&desc);
    desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
    desc.vendor_id = 0x2dc8;
    desc.product_id = 0x6000;
    desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
    desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
    desc.nhats = 1;
    desc.nsensors = 2;
    desc.sensors = sensors;
    desc.button_mask = (1u << SDL_GAMEPAD_BUTTON_SOUTH) | (1u << SDL_GAMEPAD_BUTTON_EAST) |
                       (1u << SDL_GAMEPAD_BUTTON_START) | (1u << SDL_GAMEPAD_BUTTON_DPAD_LEFT) |
                       (1u << SDL_GAMEPAD_BUTTON_DPAD_RIGHT);
    desc.axis_mask = (1u << SDL_GAMEPAD_AXIS_LEFTX) | (1u << SDL_GAMEPAD_AXIS_LEFT_TRIGGER) |
                     (1u << SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);
    desc.name = "8BitDo SF30 Pro (virtual)";
    id = SDL_AttachVirtualJoystick(&desc);
    if (!id) {
        fprintf(stderr, "virtual gamepad: %s\n", SDL_GetError());
        return -1;
    }
    pad_try_open(id);
    if (!pad)
        return -1;
    virtual_joy = SDL_GetGamepadJoystick(pad);
    if (!SDL_SetGamepadSensorEnabled(pad, SDL_SENSOR_GYRO, true) && log_pad)
        fprintf(stderr, "gamepad: gyro not enabled: %s\n", SDL_GetError());
    if (!virtual_joy || !SDL_IsJoystickVirtual(SDL_GetJoystickID(virtual_joy))) {
        fprintf(stderr, "virtual gamepad: opened device is not virtual\n");
        return -1;
    }
    return 0;
}

static void flood_virtual(void)
{
    int i;
    if (!virtual_joy)
        return;
    for (i = 0; i < 48; i++) {
        float gyro[3];
        SDL_SetJoystickVirtualAxis(virtual_joy, SDL_GAMEPAD_AXIS_LEFTX,
                                   (i & 1) ? (Sint16)-20000 : (Sint16)-10000);
        SDL_SetJoystickVirtualButton(virtual_joy, SDL_GAMEPAD_BUTTON_SOUTH, (i & 1) != 0);
        gyro[0] = 0.01f;
        gyro[1] = 0.02f;
        gyro[2] = (float)i * 0.001f;
        SDL_SendJoystickVirtualSensorData(virtual_joy, SDL_SENSOR_GYRO, SDL_GetTicksNS(), gyro, 3);
    }
}

static void volume_step(float delta);

/* Drain at most one frame of events. Returns 1 if the player quit.
   The first PollEvent is what pumps joysticks; a slow return is logged. */
static int poll_frame(int interactive)
{
    SDL_Event ev;
    int budget = pad_event_budget(100000);
    int quit = 0;
    Uint64 t0 = SDL_GetPerformanceCounter();
    double ms;

    while (budget-- > 0 && SDL_PollEvent(&ev)) {
        pad_log_note(ev.type);
        if (!interactive)
            continue;
        if (ev.type == SDL_EVENT_QUIT)
            quit = 1;
        else if (ev.type == SDL_EVENT_GAMEPAD_ADDED)
            pad_try_open(ev.gdevice.which);
        else if (ev.type == SDL_EVENT_GAMEPAD_REMOVED)
            pad_try_close(ev.gdevice.which);
        else if (ev.type == SDL_EVENT_KEY_DOWN && !ev.key.repeat) {
            if (ev.key.scancode == SDL_SCANCODE_F2)
                render_set_bloom(!render_bloom_enabled());
            else if (ev.key.scancode == SDL_SCANCODE_M)
                muted = !muted;
            else if (ev.key.scancode == SDL_SCANCODE_MINUS)
                volume_step(-0.1f);
            else if (ev.key.scancode == SDL_SCANCODE_EQUALS)
                volume_step(0.1f);
        }
    }
    ms = (double)(SDL_GetPerformanceCounter() - t0) * 1000.0 / (double)SDL_GetPerformanceFrequency();
    if (ms > soak_worst_poll_ms)
        soak_worst_poll_ms = ms;
    if (log_pad && ms > 50.0)
        fprintf(stderr, "gamepad: poll %.1f ms\n", ms);
    if (virtual_joy && ms > 250.0)
        soak_fail = 1;
    pad_log_flush(0);
    return quit;
}

static int flyer_count_alive(const GameState *g)
{
    int n = 0;
    int i;
    for (i = 0; i < STARPOST_MAX_ENEMIES; i++) {
        if (g->enemies[i].alive && g->enemies[i].kind == ENEMY_FLYER)
            n++;
    }
    return n;
}

static void audio_open(void)
{
    SDL_AudioSpec spec;
    SDL_zero(spec);
    spec.format = SDL_AUDIO_F32;
    spec.channels = 1;
    spec.freq = SYNTH_RATE;
    synth_init(&synth, 1);
    synth_set_gain(&synth, volume);
    audio_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL);
    if (!audio_stream) {
        fprintf(stderr, "audio: %s\n", SDL_GetError());
        return;
    }
    if (!SDL_ResumeAudioStreamDevice(audio_stream))
        fprintf(stderr, "audio resume: %s\n", SDL_GetError());
}

static void audio_consume(const GameState *g)
{
    int i;
    if (!g)
        return;
    for (i = 0; i < g->snd_count; i++)
        synth_trigger(&synth, g->snd[i]);
    synth_set_flyers(&synth, flyer_count_alive(g));
    synth_set_gain(&synth, muted ? 0.0f : volume);
}

static void audio_pump(void)
{
    float buf[1024];
    int target;
    int queued;
    int need;
    int frames;
    if (!audio_stream)
        return;
    target = (SYNTH_RATE / 40) * (int)sizeof(float);
    queued = SDL_GetAudioStreamQueued(audio_stream);
    need = target - queued;
    if (need < (int)sizeof(float))
        return;
    frames = need / (int)sizeof(float);
    if (frames > (int)(sizeof buf / sizeof buf[0]))
        frames = (int)(sizeof buf / sizeof buf[0]);
    synth_render(&synth, buf, frames);
    SDL_PutAudioStreamData(audio_stream, buf, frames * (int)sizeof(float));
}

static void volume_step(float delta)
{
    volume += delta;
    if (volume < 0.0f)
        volume = 0.0f;
    if (volume > 1.0f)
        volume = 1.0f;
    muted = 0;
    synth_set_gain(&synth, volume);
}

static int alive_count(const GameState *g)
{
    int n = 0;
    for (int i = 0; i < STARPOST_MAX_ENEMIES; i++)
        n += g->enemies[i].alive ? 1 : 0;
    return n;
}

static void script_input(const GameState *g, int tick, InputFrame *in, int spare_flyers)
{
    int lane = -1;
    float best = 2.0f;
    int cur, cw, ccw;
    static int hunting;

    memset(in, 0, sizeof *in);
    if (tick == 0)
        hunting = 0;
    if (g->state != STATE_PLAYING)
        return;
    if (!hunting) {
        if (alive_count(g) < 4)
            return;
        hunting = 1;
    }
    for (int i = 0; i < STARPOST_MAX_ENEMIES; i++) {
        const Enemy *e = &g->enemies[i];
        if (!e->alive || e->on_ring || e->dist >= best)
            continue;
        if (spare_flyers && e->kind == ENEMY_FLYER)
            continue;
        best = e->dist;
        lane = e->lane;
    }
    if (lane < 0)
        return;
    cur = g->player.lane;
    if (cur == lane) {
        if (g->player.cooldown <= 0.0f)
            in->fire = true;
        return;
    }
    cw = (lane - cur + STARPOST_LANES) % STARPOST_LANES;
    ccw = (cur - lane + STARPOST_LANES) % STARPOST_LANES;
    if ((tick % 2) == 0) {
        if (cw <= ccw)
            in->right = true;
        else
            in->left = true;
    }
}

static int window_pixels(SDL_Window *window, int *w, int *h)
{
    if (!SDL_GetWindowSizeInPixels(window, w, h) || *w < 1 || *h < 1) {
        fprintf(stderr, "SDL_GetWindowSizeInPixels: %s\n", SDL_GetError());
        return -1;
    }
    return 0;
}

static int run_scripted(SDL_Window *window, int frames, const char *shot,
                        const char *nobloom, const char *burst, const char *flyer,
                        const char *zapshot)
{
    GameState game;
    int saved = 0;
    int burst_saved = 0;
    int flyer_saved = 0;
    int zap_saved = 0;
    int beam_frames = 0;
    int kill_tick = -1;
    int zap_tick = -1;
    int arm_zap = 0;
    int zap_sent = 0;
    int ran = 0;
    int w = 0;
    int h = 0;
    uint32_t seen_score = 0;
    uint64_t freq = SDL_GetPerformanceFrequency();
    uint64_t t0 = SDL_GetPerformanceCounter();

    game_init(&game, 1);
    for (int tick = 0; tick < frames; tick++) {
        InputFrame in;
        poll_frame(0);
        script_input(&game, tick, &in, flyer && !flyer_saved);
        if (arm_zap) {
            in.zap = true;
            arm_zap = 0;
        }
        game_update(&game, &in, STARPOST_DT);
        audio_consume(&game);
        audio_pump();
        if (window_pixels(window, &w, &h) != 0)
            return 1;
        render_draw(&game, w, h, STARPOST_DT);
        ran++;
        if (game.score > seen_score && kill_tick < 0)
            kill_tick = tick;
        seen_score = game.score;
        {
            int flying = 0;
            int si;
            if (game.state == STATE_PLAYING) {
                for (si = 0; si < STARPOST_MAX_SHOTS; si++) {
                    if (game.shots[si].alive) {
                        flying = 1;
                        break;
                    }
                }
            }
            if (flying)
                beam_frames++;
            else
                beam_frames = 0;
        }
        if (shot && !nobloom && !saved && game.state == STATE_PLAYING) {
            float gauge = starpost_wave_gauge(&game);
            if (gauge <= 0.65f && gauge >= 0.35f && alive_count(&game) > 0) {
                if (render_screenshot(shot, w, h) != 0)
                    return 1;
                fprintf(stderr,
                        "screenshot %s (%dx%d) tick %d wave %d gauge %.2f score %u walkers %d\n",
                        shot, w, h, tick, game.cfg.wave, gauge, game.score, alive_count(&game));
                saved = 1;
            }
        }
        if (!(shot && !nobloom) && (shot || nobloom) && !saved && beam_frames == 6) {
            if (shot) {
                if (render_screenshot(shot, w, h) != 0)
                    return 1;
                fprintf(stderr,
                        "screenshot %s (%dx%d) tick %d wave %d score %u shield %d walkers %d shards %d\n",
                        shot, w, h, tick, game.cfg.wave, game.score, game.shield,
                        alive_count(&game), render_shard_count());
            }
            if (nobloom) {
                render_set_bloom(0);
                render_redraw(&game, w, h);
                if (render_screenshot(nobloom, w, h) != 0)
                    return 1;
                render_set_bloom(1);
                fprintf(stderr, "nobloom screenshot %s tick %d\n", nobloom, tick);
            }
            saved = 1;
        }
        if (burst && !burst_saved && kill_tick >= 0 && tick >= kill_tick + 18 &&
            render_shard_count() > 0) {
            if (render_screenshot(burst, w, h) != 0)
                return 1;
            burst_saved = 1;
            fprintf(stderr, "burst screenshot %s tick %d shards %d score %u\n", burst, tick,
                    render_shard_count(), game.score);
        }
        if (flyer && !flyer_saved && game.state == STATE_PLAYING) {
            for (int i = 0; i < STARPOST_MAX_ENEMIES; i++) {
                const Enemy *e = &game.enemies[i];
                if (!e->alive || e->kind != ENEMY_FLYER)
                    continue;
                if (e->dist < 0.62f && e->dist > 0.38f) {
                    if (render_screenshot(flyer, w, h) != 0)
                        return 1;
                    flyer_saved = 1;
                    fprintf(stderr,
                            "flyer screenshot %s tick %d wave %d dist %.2f lane %u energy %d\n",
                            flyer, tick, game.cfg.wave, e->dist, e->lane, game.energy);
                    break;
                }
            }
        }
        if (flyer_saved && zapshot && !zap_sent &&
            game.energy >= energy_tune()->zap_cost && alive_count(&game) > 0) {
            arm_zap = 1;
            zap_sent = 1;
        }
        if (zap_tick < 0 && game.zap_flash > 0.0f)
            zap_tick = tick;
        if (zapshot && !zap_saved && zap_tick >= 0 && tick >= zap_tick + 6) {
            if (render_screenshot(zapshot, w, h) != 0)
                return 1;
            zap_saved = 1;
            fprintf(stderr, "zap screenshot %s tick %d flash %.3f shards %d\n", zapshot, tick,
                    game.zap_flash, render_shard_count());
        }
        SDL_GL_SwapWindow(window);
        if ((shot || nobloom || burst || flyer || zapshot) &&
            ((!shot && !nobloom) || saved) && (!burst || burst_saved) &&
            (!flyer || flyer_saved) && (!zapshot || zap_saved))
            break;
    }
    {
        double sec = (double)(SDL_GetPerformanceCounter() - t0) / (double)freq;
        if (sec > 0.0 && ran > 0)
            fprintf(stderr, "scripted %d frames in %.2f ms (%.1f fps)\n", ran, sec * 1000.0,
                    (double)ran / sec);
    }
    if ((shot || nobloom) && !saved) {
        if (shot && !nobloom)
            fprintf(stderr, "screenshot: mid-wave gauge was not reached\n");
        else
            fprintf(stderr, "screenshot: pulse beam was not drawn\n");
        return 1;
    }
    if (burst && !burst_saved) {
        fprintf(stderr, "screenshot: explosion burst was not reached\n");
        return 1;
    }
    if (flyer && !flyer_saved) {
        fprintf(stderr, "screenshot: flyer was not seen mid-spiral\n");
        return 1;
    }
    if (zapshot && !zap_saved) {
        fprintf(stderr, "screenshot: superzap flash was not reached\n");
        return 1;
    }
    return 0;
}

static int save_view(SDL_Window *window, Session *session, const char *path)
{
    int w;
    int h;
    if (window_pixels(window, &w, &h) != 0)
        return 1;
    render_session(session, w, h, 0.0f);
    SDL_GL_SwapWindow(window);
    if (render_screenshot(path, w, h) != 0)
        return 1;
    fprintf(stderr, "screenshot %s (%dx%d) screen %d\n", path, w, h, (int)session->screen);
    return 0;
}

static int run_meta_shots(SDL_Window *window, const char *title, const char *pause,
                          const char *initials, const char *scores)
{
    Session session;
    if (title) {
        session_init(&session, 1);
        session.attract = 2.4f;
        if (save_view(window, &session, title) != 0)
            return 1;
    }
    if (pause) {
        session_init(&session, 1);
        game_init_difficulty(&session.game, 3, DIFF_ADVANCED);
        session.game.enemies[0].alive = true;
        session.game.enemies[0].kind = ENEMY_WALKER;
        session.game.enemies[0].lane = 2;
        session.game.enemies[0].dist = 0.62f;
        session.game.enemies[1].alive = true;
        session.game.enemies[1].kind = ENEMY_WALKER;
        session.game.enemies[1].lane = 5;
        session.game.enemies[1].dist = 0.38f;
        session.screen = SCREEN_PAUSE;
        session.pause_item = PAUSE_RESUME;
        if (save_view(window, &session, pause) != 0)
            return 1;
    }
    if (initials) {
        session_init(&session, 1);
        session.screen = SCREEN_INITIALS;
        session.initials[0] = 'D';
        session.initials[1] = 'A';
        session.initials[2] = 'N';
        session.initial_slot = SCORE_INITIALS;
        session.pending_score = 2400;
        if (save_view(window, &session, initials) != 0)
            return 1;
    }
    if (scores) {
        session_init(&session, 1);
        scores_insert(&session.scores, DIFF_ADVANCED, "DAN", 2400);
        scores_insert(&session.scores, DIFF_ADVANCED, "MAX", 1800);
        scores_insert(&session.scores, DIFF_ADVANCED, "ACE", 900);
        session.screen = SCREEN_SCORES;
        session.score_diff = DIFF_ADVANCED;
        if (save_view(window, &session, scores) != 0)
            return 1;
    }
    return 0;
}

static int save_game_view(SDL_Window *window, GameState *game, const char *path)
{
    int w;
    int h;
    if (window_pixels(window, &w, &h) != 0)
        return 1;
    render_draw(game, w, h, 0.016f);
    SDL_GL_SwapWindow(window);
    if (render_screenshot(path, w, h) != 0)
        return 1;
    fprintf(stderr, "screenshot %s (%dx%d) energy %d\n", path, w, h, game->energy);
    return 0;
}

static void place_walker(GameState *g, int slot, int lane, float dist)
{
    Enemy *e = &g->enemies[slot];
    memset(e, 0, sizeof *e);
    e->kind = ENEMY_WALKER;
    e->alive = true;
    e->lane = (uint8_t)lane;
    e->dist = dist;
    e->speed = 0.0f;
}

static void place_kind(GameState *g, int slot, EnemyKind kind, int lane, float dist)
{
    Enemy *e = &g->enemies[slot];
    memset(e, 0, sizeof *e);
    e->kind = kind;
    e->alive = true;
    e->lane = (uint8_t)lane;
    e->dist = dist;
    e->speed = g->cfg.enemy_speed;
    if (kind == ENEMY_SPLITTER)
        e->speed *= STARPOST_SPLITTER_SPEED_SCALE;
    e->angle = starpost_lane_center(lane);
}

static int run_splitter_shots(SDL_Window *window, const char *splitter, const char *split)
{
    GameState game;
    InputFrame in;
    if (splitter) {
        game_init(&game, 1);
        game.spawn_timer = 1000.0f;
        game.player.lane = 0;
        place_kind(&game, 0, ENEMY_SPLITTER, 0, 0.58f);
        place_kind(&game, 1, ENEMY_WALKER, 3, 0.72f);
        place_kind(&game, 2, ENEMY_WALKER, 5, 0.40f);
        if (save_game_view(window, &game, splitter) != 0)
            return 1;
    }
    if (split) {
        game_init(&game, 1);
        game.spawn_timer = 1000.0f;
        game.player.lane = 0;
        place_kind(&game, 0, ENEMY_SPLITTER, 0, 0.58f);
        place_kind(&game, 1, ENEMY_WALKER, 4, 0.46f);
        memset(&in, 0, sizeof in);
        in.fire = true;
        game_update(&game, &in, 0.58f / STARPOST_SHOT_SPEED + 0.004f);
        if (save_game_view(window, &game, split) != 0)
            return 1;
    }
    return 0;
}

static int run_projectile_shots(SDL_Window *window, const char *flight, const char *volley)
{
    GameState game;
    InputFrame in;
    if (flight) {
        game_init(&game, 1);
        game.spawn_timer = 1000.0f;
        game.player.lane = 0;
        place_kind(&game, 0, ENEMY_WALKER, 0, 0.92f);
        place_kind(&game, 1, ENEMY_WALKER, 0, 0.78f);
        memset(&in, 0, sizeof in);
        in.fire = true;
        game_update(&game, &in, 0.18f);
        game.player.cooldown = 0.0f;
        game_update(&game, &in, 0.10f);
        if (save_game_view(window, &game, flight) != 0)
            return 1;
    }
    if (volley) {
        game_init(&game, 1);
        game.spawn_timer = 1000.0f;
        game.player.lane = 0;
        place_kind(&game, 0, ENEMY_WALKER, 7, 0.78f);
        place_kind(&game, 1, ENEMY_WALKER, 0, 0.70f);
        place_kind(&game, 2, ENEMY_WALKER, 1, 0.84f);
        memset(&in, 0, sizeof in);
        in.spread = true;
        game_update(&game, &in, 0.16f);
        if (save_game_view(window, &game, volley) != 0)
            return 1;
    }
    return 0;
}

static int run_ability_shots(SDL_Window *window, const char *spread, const char *energize,
                             const char *slow)
{
    GameState game;
    InputFrame in;
    if (spread) {
        game_init(&game, 1);
        game.spawn_timer = 1000.0f;
        game.player.lane = 0;
        place_walker(&game, 0, 7, 0.55f);
        place_walker(&game, 1, 0, 0.48f);
        place_walker(&game, 2, 1, 0.62f);
        place_walker(&game, 3, 3, 0.40f);
        game.energy = energy_tune()->wave_grant;
        memset(&in, 0, sizeof in);
        in.spread = true;
        game_update(&game, &in, STARPOST_DT);
        if (save_game_view(window, &game, spread) != 0)
            return 1;
    }
    if (energize) {
        game_init(&game, 2);
        game.spawn_timer = 1000.0f;
        game.player.lane = 2;
        place_walker(&game, 0, 2, 0.50f);
        place_walker(&game, 1, 5, 0.45f);
        game.energy = energy_tune()->wave_grant;
        memset(&in, 0, sizeof in);
        in.energize = true;
        game_update(&game, &in, STARPOST_DT);
        if (save_game_view(window, &game, energize) != 0)
            return 1;
    }
    if (slow) {
        game_init(&game, 3);
        game.spawn_timer = 1000.0f;
        place_walker(&game, 0, 1, 0.70f);
        place_walker(&game, 1, 4, 0.42f);
        place_walker(&game, 2, 6, 0.58f);
        game.slow_timer = energy_tune()->slow_time;
        game.energy = energy_tune()->wave_grant - energy_tune()->slow_cost;
        if (save_game_view(window, &game, slow) != 0)
            return 1;
    }
    return 0;
}

static int run_interactive(SDL_Window *window, double soak_sec)
{
    Session session;
    char score_path[512];
    uint64_t freq = SDL_GetPerformanceFrequency();
    uint64_t prev = SDL_GetPerformanceCounter();
    uint64_t soak_deadline = 0;
    double acc = 0.0;
    int running = 1;
    uint32_t seed = (uint32_t)prev;
    if (seed == 0)
        seed = 1;
    session_init(&session, seed);
    score_path[0] = '\0';
    if (scores_default_path(score_path, (int)sizeof score_path) == 0)
        scores_load(&session.scores, score_path);
    else
        fprintf(stderr, "scores: no home directory, this session will not be saved\n");
    if (soak_sec > 0.0)
        soak_deadline = prev + (uint64_t)(soak_sec * (double)freq);

    while (running) {
        SessionInput sin;
        const bool *keys;
        uint64_t now;
        double elapsed;
        int steps;
        int w, h;

        if (soak_deadline)
            flood_virtual();
        if (poll_frame(1))
            running = 0;
        if (!running)
            break;

        keys = SDL_GetKeyboardState(NULL);
        memset(&sin, 0, sizeof sin);
        sin.play.left = keys[SDL_SCANCODE_LEFT] || keys[SDL_SCANCODE_A];
        sin.play.right = keys[SDL_SCANCODE_RIGHT] || keys[SDL_SCANCODE_D];
        sin.play.fire = keys[SDL_SCANCODE_SPACE] || keys[SDL_SCANCODE_J];
        sin.play.zap = keys[SDL_SCANCODE_Z] || keys[SDL_SCANCODE_LSHIFT] ||
                       keys[SDL_SCANCODE_RSHIFT];
        sin.play.spread = keys[SDL_SCANCODE_X];
        sin.play.energize = keys[SDL_SCANCODE_Q];
        sin.play.slow = keys[SDL_SCANCODE_E];
        sin.play.repair = keys[SDL_SCANCODE_R];
        sin.up = keys[SDL_SCANCODE_UP] || keys[SDL_SCANCODE_W];
        sin.down = keys[SDL_SCANCODE_DOWN] || keys[SDL_SCANCODE_S];
        sin.accept = keys[SDL_SCANCODE_RETURN] || keys[SDL_SCANCODE_KP_ENTER] ||
                     keys[SDL_SCANCODE_SPACE] || keys[SDL_SCANCODE_J];
        sin.pause = keys[SDL_SCANCODE_ESCAPE] || keys[SDL_SCANCODE_P];
        if (pad) {
            PadState axes;
            Sint16 axis;
            float stick_y;
            memset(&axes, 0, sizeof axes);
            axis = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTX);
            axes.stick_x = axis / 32767.0f;
            pad_stick_hold(axes.stick_x, &stick_hold_left, &stick_hold_right);
            axes.stick_x = stick_hold_left ? -1.0f : (stick_hold_right ? 1.0f : 0.0f);
            axes.dpad_left = SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_LEFT);
            axes.dpad_right = SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_RIGHT);
            axes.a = SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_SOUTH);
            axes.b = SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_EAST);
            axes.x = SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_WEST);
            axes.y = SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_NORTH);
            axes.lb = SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER);
            axes.rb = SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER);
            axis = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);
            axes.rt = axis > 0 ? axis / 32767.0f : 0.0f;
            axis = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER);
            axes.lt = axis > 0 ? axis / 32767.0f : 0.0f;
            axes.start = SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_START);
            pad_apply(&sin.play, &axes);
            axis = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTY);
            stick_y = axis / 32767.0f;
            if (SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_UP) || stick_y < -PAD_DEADZONE)
                sin.up = true;
            if (SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_DOWN) || stick_y > PAD_DEADZONE)
                sin.down = true;
            if (axes.a)
                sin.accept = true;
            if (axes.start)
                sin.pause = true;
        }

        now = SDL_GetPerformanceCounter();
        elapsed = (double)(now - prev) / (double)freq;
        prev = now;
        steps = game_frame_steps(&acc, elapsed);
        {
            int ran = steps;
            int simulated = 0;
            while (steps-- > 0) {
                if (session_update(&session, &sin, STARPOST_DT)) {
                    audio_consume(&session.game);
                    simulated = 1;
                }
            }
            if (!simulated &&
                (session.screen == SCREEN_PLAY || session.screen == SCREEN_PAUSE))
                synth_set_flyers(&synth, flyer_count_alive(&session.game));
            if (session.scores_dirty && score_path[0]) {
                if (scores_save(&session.scores, score_path) != 0)
                    fprintf(stderr, "scores: could not save %s\n", score_path);
                session.scores_dirty = 0;
            }
            audio_pump();

            if (window_pixels(window, &w, &h) != 0)
                return 1;
            render_session(&session, w, h, ran > 0 ? (float)ran * STARPOST_DT : (float)elapsed);
        }
        SDL_GL_SwapWindow(window);
        if (session.quit)
            break;
        if (soak_deadline && SDL_GetPerformanceCounter() >= soak_deadline)
            break;
    }
    if (soak_sec > 0.0) {
        fprintf(stderr, "soak-pad %.2fs worst poll %.1f ms %s\n", soak_sec, soak_worst_poll_ms,
                soak_fail ? "FAIL" : "ok");
        if (!pad || !virtual_joy)
            return 1;
        return soak_fail ? 1 : 0;
    }
    return 0;
}

int main(int argc, char **argv)
{
    int frames = 0;
    const char *shot = NULL;
    const char *nobloom = NULL;
    const char *burst = NULL;
    const char *flyer = NULL;
    const char *zapshot = NULL;
    const char *title_shot = NULL;
    const char *pause_shot = NULL;
    const char *initials_shot = NULL;
    const char *scores_shot = NULL;
    const char *spread_shot = NULL;
    const char *energize_shot = NULL;
    const char *slow_shot = NULL;
    const char *splitter_shot = NULL;
    const char *split_shot = NULL;
    const char *flight_shot = NULL;
    const char *volley_shot = NULL;
    int parsed;
    int scripted;
    int meta;
    int ability;
    int splitter_mode;
    int projectile_mode;
    double soak = 0.0;
    SDL_Window *window;
    SDL_GLContext gl;
    int rc;

    parsed = parse_args(argc, argv, &frames, &shot, &nobloom, &burst, &flyer, &zapshot, &soak,
                        &title_shot, &pause_shot, &initials_shot, &scores_shot, &spread_shot,
                        &energize_shot, &slow_shot, &splitter_shot, &split_shot, &flight_shot,
                        &volley_shot);
    if (parsed != 0)
        return parsed < 0 ? 1 : 0;
    meta = title_shot || pause_shot || initials_shot || scores_shot;
    ability = spread_shot || energize_shot || slow_shot;
    splitter_mode = splitter_shot || split_shot;
    projectile_mode = flight_shot || volley_shot;
    scripted = soak <= 0.0 && (frames > 0 || shot != NULL || nobloom != NULL || burst != NULL ||
                               flyer != NULL || zapshot != NULL);
    if ((shot || nobloom || burst || flyer || zapshot) && frames <= 0)
        frames = (flyer || zapshot) ? 3600 : 2400;

    configure_gamepad();
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO))
        fprintf(stderr, "audio init: %s\n", SDL_GetError());
    else
        audio_open();
    if (!SDL_InitSubSystem(SDL_INIT_GAMEPAD))
        fprintf(stderr, "gamepad init: %s\n", SDL_GetError());
    if (soak > 0.0) {
        if (attach_soak_pad() != 0) {
            SDL_Quit();
            return 1;
        }
    } else {
        int pad_count = 0;
        SDL_JoystickID *pads = SDL_GetGamepads(&pad_count);
        pad_log_devices();
        if (pads && pad_count > 0)
            pad_try_open(pads[0]);
        SDL_free(pads);
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);
    SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);

    window = SDL_CreateWindow("Starpost Returns", 960, 720,
                              SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE |
                                  SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!window) {
        fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    gl = SDL_GL_CreateContext(window);
    if (!gl) {
        fprintf(stderr, "SDL_GL_CreateContext: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    SDL_GL_SetSwapInterval(scripted ? 0 : 1);
    if (render_init() != 0) {
        SDL_GL_DestroyContext(gl);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    rc = 0;
    if (meta)
        rc = run_meta_shots(window, title_shot, pause_shot, initials_shot, scores_shot);
    if (rc == 0 && ability)
        rc = run_ability_shots(window, spread_shot, energize_shot, slow_shot);
    if (rc == 0 && splitter_mode)
        rc = run_splitter_shots(window, splitter_shot, split_shot);
    if (rc == 0 && projectile_mode)
        rc = run_projectile_shots(window, flight_shot, volley_shot);
    if (rc == 0 && scripted)
        rc = run_scripted(window, frames, shot, nobloom, burst, flyer, zapshot);
    else if (rc == 0 && !meta && !ability && !splitter_mode && !projectile_mode)
        rc = run_interactive(window, soak);
    if (pad)
        SDL_CloseGamepad(pad);
    if (audio_stream)
        SDL_DestroyAudioStream(audio_stream);
    SDL_GL_DestroyContext(gl);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return rc;
}
