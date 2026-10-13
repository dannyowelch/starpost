# Starpost Returns

Unofficial neon-vector sequel to Commodore's *Star Post* (VIC-20 / C64). A base sits at the centre of eight lanes. Walkers come in from the rim in waves that get longer, faster, and denser. You rotate a tracer one lane at a time and fire an instant pulse beam. Kills score. Clearing a wave recharges the shield a little, adds a bonus, and shows a short clear screen. The shield persists until it is empty.

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

Headless smoke test (no window manager). This steps a fixed 120 Hz simulation and can write the same mid-wave frame with bloom on and off, plus an explosion:

```sh
SDL_VIDEODRIVER=offscreen ./build/starpost --frames 2400 \
  --screenshot bloom.png --nobloom-screenshot flat.png --burst-screenshot burst.png
```

`--frames 600` with no screenshot prints how many frames per second the machine managed.

If the offscreen driver cannot create an OpenGL context, an X dummy server works the same way:

```sh
SDL_VIDEODRIVER=x11 xvfb-run -a ./build/starpost --frames 600
```

## Controls

| Key | Action |
| --- | --- |
| Left / A | Rotate one lane counter-clockwise |
| Right / D | Rotate one lane clockwise |
| Space / J | Fire the pulse beam down the tracer's lane |
| F2 | Toggle bloom |
| Enter, R, or any other key except Esc | Restart after game over |
| Esc | Quit |

Hold left or right to keep rotating. The beam destroys the nearest walker in that lane and scores 100. A walker that reaches the base drains the shield and does not score. Clearing the wave's quota shows WAVE CLEAR for a moment, adds 500, and restores 2 shield points up to the cap of 8. At zero shield the base turns red and the run stops until you restart. The HUD shows the wave, the score, and a shield bar.

## Layout

- `src/game/` — simulation. No SDL or OpenGL, so the tests run headless.
- `src/platform/` — window, input, fixed timestep.
- `src/fx/` — explosion fragments and screen shake. No SDL or OpenGL.
- `src/render/` — OpenGL 3.3 lines, HDR target, and bloom.
- `tests/` — game rules, plus fragment motion, the burst cap, and shake decay.
