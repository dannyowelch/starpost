#include "audio/synth.h"
#include "game/game.h"
#include "input/pad.h"
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
            "  Space or J         fire\n"
            "  Z or Shift         superzap\n"
            "  M                  mute\n"
            "  - / =              volume\n"
            "  F2                 toggle bloom\n"
            "  Enter, R, or any other key except Esc  restart after game over\n"
            "  Esc                quit\n"
            "  --soak-pad SEC     run SEC seconds with a virtual gamepad\n"
            "  STARPOST_LOG_GAMEPAD=1  log pad identity, event counts, slow polls\n");
}

static int parse_args(int argc, char **argv, int *frames, const char **shot,
                      const char **nobloom, const char **burst, const char **flyer,
                      const char **zapshot, double *soak)
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
static int poll_frame(int interactive, int game_over, int *confirm)
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
            else if (ev.key.scancode == SDL_SCANCODE_ESCAPE)
                quit = 1;
            else if (game_over && confirm)
                *confirm = 1;
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
    if (g->state != STATE_PLAYING || g->player.beam_timer > 0.0f)
        return;
    if (!hunting) {
        if (alive_count(g) < 4)
            return;
        hunting = 1;
    }
    for (int i = 0; i < STARPOST_MAX_ENEMIES; i++) {
        const Enemy *e = &g->enemies[i];
        if (!e->alive || e->dist >= best)
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
        poll_frame(0, 0, NULL);
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
        if (game.state == STATE_PLAYING && game.player.beam_timer > 0.0f)
            beam_frames++;
        else
            beam_frames = 0;
        if ((shot || nobloom) && !saved && beam_frames == 6) {
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
                            "flyer screenshot %s tick %d wave %d dist %.2f lane %u zaps %u\n",
                            flyer, tick, game.cfg.wave, e->dist, e->lane, game.player.zaps);
                    break;
                }
            }
        }
        if (flyer_saved && zapshot && !zap_sent && game.player.zaps > 0 && alive_count(&game) > 0) {
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

static int run_interactive(SDL_Window *window, double soak_sec)
{
    GameState game;
    uint64_t freq = SDL_GetPerformanceFrequency();
    uint64_t prev = SDL_GetPerformanceCounter();
    uint64_t soak_deadline = 0;
    double acc = 0.0;
    int running = 1;
    uint32_t seed = (uint32_t)prev;
    if (seed == 0)
        seed = 1;
    game_init(&game, seed);
    if (soak_sec > 0.0)
        soak_deadline = prev + (uint64_t)(soak_sec * (double)freq);

    while (running) {
        InputFrame in;
        const bool *keys;
        int confirm = 0;
        uint64_t now;
        double elapsed;
        int steps;
        int w, h;

        if (soak_deadline)
            flood_virtual();
        if (poll_frame(1, game.state == STATE_GAME_OVER, &confirm))
            running = 0;
        if (!running)
            break;

        keys = SDL_GetKeyboardState(NULL);
        memset(&in, 0, sizeof in);
        if (game.state == STATE_GAME_OVER) {
            in.confirm = confirm;
        } else {
            in.left = keys[SDL_SCANCODE_LEFT] || keys[SDL_SCANCODE_A];
            in.right = keys[SDL_SCANCODE_RIGHT] || keys[SDL_SCANCODE_D];
            in.fire = keys[SDL_SCANCODE_SPACE] || keys[SDL_SCANCODE_J];
            in.zap = keys[SDL_SCANCODE_Z] || keys[SDL_SCANCODE_LSHIFT] ||
                     keys[SDL_SCANCODE_RSHIFT];
        }
        if (pad) {
            PadState axes;
            Sint16 axis;
            memset(&axes, 0, sizeof axes);
            axis = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTX);
            axes.stick_x = axis / 32767.0f;
            pad_stick_hold(axes.stick_x, &stick_hold_left, &stick_hold_right);
            axes.stick_x = stick_hold_left ? -1.0f : (stick_hold_right ? 1.0f : 0.0f);
            axes.dpad_left = SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_LEFT);
            axes.dpad_right = SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_RIGHT);
            axes.a = SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_SOUTH);
            axes.b = SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_EAST);
            axis = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);
            axes.rt = axis > 0 ? axis / 32767.0f : 0.0f;
            axis = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER);
            axes.lt = axis > 0 ? axis / 32767.0f : 0.0f;
            axes.start = SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_START);
            pad_apply(&in, &axes);
        }

        now = SDL_GetPerformanceCounter();
        elapsed = (double)(now - prev) / (double)freq;
        prev = now;
        steps = game_frame_steps(&acc, elapsed);
        {
            int ran = steps;
            while (steps-- > 0) {
                game_update(&game, &in, STARPOST_DT);
                audio_consume(&game);
            }
            if (ran == 0)
                synth_set_flyers(&synth, flyer_count_alive(&game));
            audio_pump();

            if (window_pixels(window, &w, &h) != 0)
                return 1;
            render_draw(&game, w, h, ran > 0 ? (float)ran * STARPOST_DT : (float)elapsed);
        }
        SDL_GL_SwapWindow(window);
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
    int parsed;
    int scripted;
    double soak = 0.0;
    SDL_Window *window;
    SDL_GLContext gl;
    int rc;

    parsed = parse_args(argc, argv, &frames, &shot, &nobloom, &burst, &flyer, &zapshot, &soak);
    if (parsed != 0)
        return parsed < 0 ? 1 : 0;
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

    rc = scripted ? run_scripted(window, frames, shot, nobloom, burst, flyer, zapshot)
                  : run_interactive(window, soak);
    if (pad)
        SDL_CloseGamepad(pad);
    if (audio_stream)
        SDL_DestroyAudioStream(audio_stream);
    SDL_GL_DestroyContext(gl);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return rc;
}
