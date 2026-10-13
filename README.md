# Starpost Returns

Unofficial neon-vector sequel to Commodore's *Star Post* (VIC-20 / C64). A base sits at the centre of eight lanes. Walkers come in from the rim in waves that get longer, faster, and denser. You rotate a tracer one lane at a time and fire an instant pulse beam. Kills score. Clearing a wave adds a bonus and shows a short clear screen. The shield persists until it is empty; a pip can be bought back with energy.

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
| Up / Down or W / S | Move through menus and initials |
| Left / A | Rotate one lane counter-clockwise, or change an initials slot |
| Right / D | Rotate one lane clockwise, or change an initials slot |
| Space / J / Enter | Fire in play. Accept a menu item. On initials, move to the next column, or save when END is selected |
| X | Spread shot, 2 energy. Current lane and both neighbors |
| Q | Energize the aimed lane, 5 energy, 3 seconds |
| E | Slow walkers, 10 energy, 5 seconds. Flyers are not slowed |
| R | Repair one shield pip, 10 energy |
| Z or Shift | Superzap, 20 energy. No longer a stored charge |
| M | Mute |
| `-` / `=` | Volume down / up |
| F2 | Toggle bloom |
| Esc or P | Pause during play. Esc also resumes or leaves a menu. On initials it matches Enter: next column, or save on END |

The game opens on a title menu: Start, Difficulty, High Scores, Quit. Difficulty is Beginner, Advanced, or Hyper. High scores are the top 10 for each difficulty, saved under `$XDG_DATA_HOME/starpost-returns/scores.txt` (or `~/.local/share/starpost-returns/scores.txt`).

A connected gamepad is picked up when it is plugged in. D-pad or left stick left/right steps one lane (stick deadzone 0.35, held until the stick returns inside 0.20, then the same hold-repeat as the keys). A or the right trigger fires. Y or the left trigger superzaps (20 energy). B energizes the aimed lane (5 energy, 3 seconds). X is the spread shot (2 energy). The right shoulder slows walkers (10 energy, 5 seconds). The left shoulder repairs one shield pip (10 energy). Start pauses during play. The HUD shows `EN` for energy. A run starts at 20. A normal shot costs 1, a walker kill returns 1, a flyer kill returns 5, and a cleared wave adds 10, up to 100. At 0 energy the shot is refused and the tracer turns red. The stick does not choose a lane directly. Keyboard and pad can be used together. The Switch, 8BitDo, and Xbox HIDAPI drivers stay off so an 8BitDo SF30 Pro is read from the kernel device instead of a blocking hidraw write on the poll thread. `STARPOST_LOG_GAMEPAD=1` logs the pad and any poll that takes longer than 50 ms.

Hold left or right to keep rotating. The beam destroys the nearest walker in that lane (100 points) or a flyer while it is inside the tracer's lane window (300 points). A walker at the base drains 1 shield; a flyer drains 3. From wave 2 a yellow flyer spirals across the lanes. The HUD shows energy as `EN`. Clearing a wave adds 10, up to 100. Superzap spends 20 of it and clears everything on screen. The bar at the top counts down the wave: it starts full and empties as walkers and flyers are killed or reach the base. Shield is the pips around the base. Superzap clears everything on screen. Clearing a wave shows WAVE CLEAR and adds 500. It does not restore shield. R or the left shoulder buys one pip for 10 energy, up to 8, during a wave or on the clear screen. At zero shield the run ends. A score inside the top 10 asks for three initials. Confirm moves to the next column, and the score is saved only from the END column. Entry ignores input for 1 second and does not reuse a button still held from the game. Then the title returns. Esc pauses; it does not quit. The pause menu can resume or return to the title. Advanced matches the base wave table. Beginner is slower and sparser, with flyers from wave 4. Hyper is faster and denser, with flyers from wave 1. Neither difficulty restores shield on a clear.

## Layout

- `src/game/` — simulation. No SDL or OpenGL, so the tests run headless.
- `src/meta/` — title, pause, difficulty, initials, and the score file. No SDL or OpenGL.
- `src/audio/` — procedural synth. No SDL and no samples.
- `src/input/` — gamepad axes to the shared input frame, including the stick deadzone.
- `src/platform/` — window, keyboard, gamepad hot-plug, audio stream, fixed timestep.
- `src/fx/` — explosion fragments and screen shake. No SDL or OpenGL.
- `src/render/` — OpenGL 3.3 lines, HDR target, and bloom.
- `tests/` — game rules, synth output, pad mapping, fragments, and shake.
