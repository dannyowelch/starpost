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
| Z or Shift | Superzap, if a charge is stored |
| M | Mute |
| `-` / `=` | Volume down / up |
| F2 | Toggle bloom |
| Enter, R, or any other key except Esc | Restart after game over |
| Esc | Quit |

A connected gamepad is picked up when it is plugged in. D-pad or left stick left/right steps one lane (stick deadzone 0.35, held until the stick returns inside 0.20, then the same hold-repeat as the keys). A or the right trigger fires. B or the left trigger superzaps. Start restarts after game over. The stick does not choose a lane directly. Keyboard and pad can be used together. The Switch, 8BitDo, and Xbox HIDAPI drivers stay off so an 8BitDo SF30 Pro is read from the kernel device instead of a blocking hidraw write on the poll thread. `STARPOST_LOG_GAMEPAD=1` logs the pad and any poll that takes longer than 50 ms.

Hold left or right to keep rotating. The beam destroys the nearest walker in that lane (100 points) or a flyer while it is inside the tracer's lane window (300 points). A walker at the base drains 1 shield; a flyer drains 3. From wave 2 a yellow flyer spirals across the lanes. Each wave grants one superzap charge, kept up to 4, shown as `ZAP` in the HUD. The bar at the top counts down the wave: it starts full and empties as walkers and flyers are killed or reach the base. Shield is the pips around the base. Superzap clears everything on screen. Clearing a wave shows WAVE CLEAR, adds 500, and restores 2 shield up to 8. At zero shield the base turns red until you restart.

## Layout

- `src/game/` — simulation. No SDL or OpenGL, so the tests run headless.
- `src/audio/` — procedural synth. No SDL and no samples.
- `src/input/` — gamepad axes to the shared input frame, including the stick deadzone.
- `src/platform/` — window, keyboard, gamepad hot-plug, audio stream, fixed timestep.
- `src/fx/` — explosion fragments and screen shake. No SDL or OpenGL.
- `src/render/` — OpenGL 3.3 lines, HDR target, and bloom.
- `tests/` — game rules, synth output, pad mapping, fragments, and shake.
