#include "game/game.h"
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
            "  Left/Right or A/D  rotate one lane\n"
            "  Space or J         fire\n"
            "  F2                 toggle bloom\n"
            "  Enter, R, or any other key except Esc  restart after game over\n"
            "  Esc                quit\n");
}

static int parse_args(int argc, char **argv, int *frames, const char **shot,
                      const char **nobloom, const char **burst)
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

static int alive_count(const GameState *g)
{
    int n = 0;
    for (int i = 0; i < STARPOST_MAX_ENEMIES; i++)
        n += g->enemies[i].alive ? 1 : 0;
    return n;
}

static void script_input(const GameState *g, int tick, InputFrame *in)
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
                        const char *nobloom, const char *burst)
{
    GameState game;
    int saved = 0;
    int burst_saved = 0;
    int beam_frames = 0;
    int kill_tick = -1;
    int ran = 0;
    int w = 0;
    int h = 0;
    uint32_t seen_score = 0;
    uint64_t freq = SDL_GetPerformanceFrequency();
    uint64_t t0 = SDL_GetPerformanceCounter();

    game_init(&game, 1);
    for (int tick = 0; tick < frames; tick++) {
        InputFrame in;
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
        }
        script_input(&game, tick, &in);
        game_update(&game, &in, STARPOST_DT);
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
        SDL_GL_SwapWindow(window);
        if ((shot || nobloom || burst) && ((!shot && !nobloom) || saved) &&
            (!burst || burst_saved))
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
    return 0;
}

static int run_interactive(SDL_Window *window)
{
    GameState game;
    uint64_t freq = SDL_GetPerformanceFrequency();
    uint64_t prev = SDL_GetPerformanceCounter();
    double acc = 0.0;
    int running = 1;
    uint32_t seed = (uint32_t)prev;
    if (seed == 0)
        seed = 1;
    game_init(&game, seed);

    while (running) {
        SDL_Event ev;
        InputFrame in;
        const bool *keys;
        int confirm = 0;
        uint64_t now;
        double elapsed;
        int steps = 0;
        int w, h;

        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_EVENT_QUIT)
                running = 0;
            else if (ev.type == SDL_EVENT_KEY_DOWN && !ev.key.repeat) {
                if (ev.key.scancode == SDL_SCANCODE_F2)
                    render_set_bloom(!render_bloom_enabled());
                else if (ev.key.scancode == SDL_SCANCODE_ESCAPE)
                    running = 0;
                else if (game.state == STATE_GAME_OVER)
                    confirm = 1;
            }
        }
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
        }

        now = SDL_GetPerformanceCounter();
        elapsed = (double)(now - prev) / (double)freq;
        prev = now;
        if (elapsed > 0.25)
            elapsed = 0.25;
        acc += elapsed;
        while (acc >= (double)STARPOST_DT && steps < 8) {
            game_update(&game, &in, STARPOST_DT);
            acc -= (double)STARPOST_DT;
            steps++;
        }

        if (window_pixels(window, &w, &h) != 0)
            return 1;
        render_draw(&game, w, h,
                    steps > 0 ? (float)steps * STARPOST_DT : (float)elapsed);
        SDL_GL_SwapWindow(window);
    }
    return 0;
}

int main(int argc, char **argv)
{
    int frames = 0;
    const char *shot = NULL;
    const char *nobloom = NULL;
    const char *burst = NULL;
    int parsed;
    int scripted;
    SDL_Window *window;
    SDL_GLContext gl;
    int rc;

    parsed = parse_args(argc, argv, &frames, &shot, &nobloom, &burst);
    if (parsed != 0)
        return parsed < 0 ? 1 : 0;
    scripted = frames > 0 || shot != NULL || nobloom != NULL || burst != NULL;
    if ((shot || nobloom || burst) && frames <= 0)
        frames = 2400;

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
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

    rc = scripted ? run_scripted(window, frames, shot, nobloom, burst) : run_interactive(window);
    SDL_GL_DestroyContext(gl);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return rc;
}
