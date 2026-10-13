# Starpost Returns

Unofficial neon-vector sequel to Commodore's *Star Post* (VIC-20 / C64). A base sits at the centre of eight lanes. Walkers come in from the rim. You rotate a tracer one lane at a time and fire an instant pulse beam. The shield persists until it is empty.

Linux first (Arch / Omarchy, Wayland / Hyprland). C, SDL3, and OpenGL. No engine and no third-party assets.

Design and the decision log: [docs/DESIGN.md](docs/DESIGN.md), [docs/DECISIONS.md](docs/DECISIONS.md).

## Build (Arch)

```sh
sudo pacman -S --needed base-devel cmake pkgconf sdl3 mesa libglvnd zlib
cmake -B build
cmake --build build
```

SDL3 is found with pkg-config (`sdl3.pc` from the `sdl3` package). OpenGL comes from `mesa` / `libglvnd`.

## Run

```sh
./build/starpost_tests
ctest --test-dir build --output-on-failure
./build/starpost
```

Headless smoke test (no window manager). This steps a fixed 120 Hz simulation and can write a PNG:

```sh
SDL_VIDEODRIVER=offscreen ./build/starpost --frames 400 --screenshot shot.png
```

If the offscreen driver cannot create an OpenGL context, an X dummy server works the same way:

```sh
SDL_VIDEODRIVER=x11 xvfb-run -a ./build/starpost --frames 400 --screenshot shot.png
```

## Controls

| Key | Action |
| --- | --- |
| Left / A | Rotate one lane counter-clockwise |
| Right / D | Rotate one lane clockwise |
| Space / J | Fire the pulse beam down the tracer's lane |
| Enter, R, or any other key except Esc | Restart after game over |
| Esc | Quit |

Hold left or right to keep rotating. The beam destroys the nearest walker in that lane. A walker that reaches the base drains the shield. At zero shield the base turns red and the run stops until you restart.

## Layout

- `src/game/` — simulation. No SDL or OpenGL, so the tests run headless.
- `src/platform/` — window, input, fixed timestep.
- `src/render/` — OpenGL 3.3 lines.
- `tests/` — lane wrap, nearest hit, shield, game over, restart.
