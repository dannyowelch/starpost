# Decision Log

- **2026-10-13** — Stack: C + SDL3 + OpenGL, chosen by Danny.
- **2026-10-13** — No prebuilt game engine (Godot/Unity/etc.); small libraries OK, keep it lean.
- **2026-10-13** — Linux first, targeting Omarchy (Arch, Wayland/Hyprland).
- **2026-10-13** — Visual direction: neon vector style (Tempest-like) with bloom.
- **2026-10-13** — Assets: only Danny's or clearly licensed, credited. Nothing ripped from the original.
- **2026-10-13** — Title: **Starpost Returns**. Visual nods to the C64 original are in scope, amped up with Tempest neon flair. No ripped art or audio.
- **2026-10-13** — Fire is an instant beam. A tracer shows the current lane. Firing makes the beam rapidly widen and brighten like a pulse laser. The exact pulse shape can still be iterated.
- **2026-10-13** — Movement is one lane at a time (left/right rotation), like the original and Tempest. No direct 8-way lane select.
- **2026-10-13** — The original 8-hits-per-wave rule is replaced by a persistent shield. A walker that reaches the base drains the shield. Each cleared wave restores a tunable amount of shield (not a full reset). Empty shield is game over.
- **2026-10-13** — Superzap stays in M4 with the flyer. It is not in the core loop and it is not held for a later power-up phase.
- **2026-10-13** — M2 wave tuning is one table in `src/game/game.c`: count `5 + 3i` (cap 48), speed `0.15 + 0.025i` (cap 0.70), spawn interval `0.90 - 0.04i` (minimum 0.22), `i = wave - 1`. Later waves are longer, faster, and denser.
- **2026-10-13** — Score is 100 per kill and 500 on wave clear. Arrivals do not score. Shield recharge is applied on clear and capped at max. Empty shield skips the clear. The clear screen holds for 1.5 s, then the next wave starts.
- **2026-10-13** — The HUD (wave, score, shield bar) and the clear screen use a small stroke vector font in `src/render/render.c`. No font files.
- **2026-10-13** — M3 neon: scene in an RGBA16F target, bright-pass plus a 5-tap separable Gaussian at quarter resolution, added back and tonemapped. F2 toggles bloom. Line fragments and screen shake live in `src/fx`, spawned from a renderer frame diff, not from game-logic fields. The HUD does not shake.
