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
