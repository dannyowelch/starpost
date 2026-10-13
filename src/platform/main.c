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
            "  Left/Right or A/D  rotate one lane\n"
            "  Space or J         fire\n"
            "  Enter, R, or any other key except Esc  restart after game over\n"
            "  Esc                quit\n");
}

static int parse_args(int argc, char **argv, int *frames, const char **shot)
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

    memset(in, 0, sizeof *in);
    if (g->state != STATE_PLAYING || g->player.beam_timer > 0.0f)
        return;
    if (alive_count(g) < 5)
        return;
    for (int i = 0; i < STARPOST_MAX_ENEMIES; i++) {
        const Enemy *e = &g->enemies[i];
        if (!e->alive || e->dist >= best)
            continue;
        best = e->dist;
        lane = e->lane;
    }
    if (lane < 0 || best > 0.80f)
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

static int run_scripted(SDL_Window *window, int frames, const char *shot)
{
    GameState game;
    int saved = 0;
    int beam_frames = 0;
    int w = 0;
    int h = 0;

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
        render_draw(&game, w, h);
        if (game.player.beam_timer > 0.0f)
            beam_frames++;
        else
            beam_frames = 0;
        if (shot && !saved && beam_frames == 6) {
            if (render_screenshot(shot, w, h) != 0)
                return 1;
            saved = 1;
            fprintf(stderr, "screenshot %s (%dx%d) tick %d shield %d walkers %d lane %u\n",
                    shot, w, h, tick, game.shield, alive_count(&game), game.player.lane);
        }
        SDL_GL_SwapWindow(window);
    }
    if (shot && !saved) {
        fprintf(stderr, "screenshot: pulse beam was not drawn\n");
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
                if (ev.key.scancode == SDL_SCANCODE_ESCAPE)
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
        render_draw(&game, w, h);
        SDL_GL_SwapWindow(window);
    }
    return 0;
}

int main(int argc, char **argv)
{
    int frames = 0;
    const char *shot = NULL;
    int parsed;
    int scripted;
    SDL_Window *window;
    SDL_GLContext gl;
    int rc;

    parsed = parse_args(argc, argv, &frames, &shot);
    if (parsed != 0)
        return parsed < 0 ? 1 : 0;
    scripted = frames > 0 || shot != NULL;
    if (shot && frames <= 0)
        frames = 720;

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

    rc = scripted ? run_scripted(window, frames, shot) : run_interactive(window);
    SDL_GL_DestroyContext(gl);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return rc;
}
