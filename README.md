# Roboslop

Roboslop is an experimental but serious C++23 platform for real-time
rendering, games and simulation, AI/LLM-integrated interactive systems,
and the tools and graphical experiments that grow up around them. The
core can be used as a general game engine, but it is not designed as an
abstract "universal engine" in a vacuum: engine features are pulled into
existence by concrete applications.

This repository is a monorepo:

- `engine/` — the Roboslop engine, a static library whose public
  surface is a set of C++23 modules under the `roboslop` namespace.
- `apps/gorden/` — **Gorden**, a single-player 3D game in development
  with an LLM-controlled robot companion. Today it doubles as the
  gameplay/AI sandbox and the debug/demo app where new engine features
  are first exercised.
- `apps/shaderlab/` — **Shader Lab**, a live shader sandbox: edit a
  shader on disk, it is recompiled through `shaderc` and swapped into
  the running scene; compile errors show in a panel while the last
  working program keeps rendering.

- `apps/editor/` — a first scene editor: create primitive rooms, edit
  objects and materials, save/load JSON, and preview physics with Play/Stop.
  See [scene editing](docs/scene-editor.md).

## Status

The engine is past "hello world" but well short of a product. What
exists today, all driven by the Gorden demo:

- ECS facade over EnTT (`roboslop.ecs`).
- Semi-fixed timestep loop with a configurable fixed rate.
- Taskflow-backed system scheduling: systems declare the resources
  they read and write; the scheduler derives a DAG once and reuses it.
- Render graph over bgfx view-IDs, with a frontend/backend split: draw
  items are collected into a per-frame arena, sorted by a packed key,
  and submitted with no heap allocation in the render loop.
- Windowing and polled keyboard/mouse/gamepad input via GLFW, with a free-fly
  debug camera.
- Jolt physics as three fixed-update systems (spawn, step, sync back to
  transforms).
- Asset loading: meshes via Assimp, textures via stb_image, a
  `Material` component, and an `AssetCache` that owns GPU-side
  programs, textures, and uniforms.
- One directional light and one optional point light with Lambert shading.
- Audio foundation via miniaudio: device, 3D listener and source
  systems.
- Animation data layer: skeleton, clips, CPU clip sampling, and an
  animation-state tick system. No GPU skinning yet.

- Dev UI: Dear ImGui over GLFW + bgfx (`roboslop.ui`), opt-in per app.
- Runtime shader compilation through the `shaderc` binary and hot
  replacement of a running program.
- LLM provider abstraction (`roboslop.llm`) with a scripted backend and
  an OpenAI-compatible HTTPS backend over libcurl; asynchronous
  completions that never block the frame loop.
- In Gorden: the first agent loop. The robot receives a structured
  observation, proposes `moveTo` / `inspect` / `say` tool calls, the
  app validates them, and the simulation executes them. Talk to it in
  the "Robot" panel; put an API key in `~/.config/roboslop/llm.json`
  (or set `OPENAI_API_KEY`) for a real model, otherwise a scripted
  provider runs the same chain. Player and robot names live in
  `~/.config/roboslop/gorden.json` (Settings window).
- Developer tooling: a window registry with a View menu (F1 toggles the
  overlay), an in-memory virtual filesystem with live and host mounts,
  a small shell, and a Terminal window. In Gorden, `tail -f
  /var/log/agent.log` follows the agent; `/persist` is a real directory
  under `~/.local/share/roboslop/gorden/`.

Gorden loads its environment from a shared scene file and has agent memory
and explicit save/load (M3's first slice); further reflection and replay
remain unfinished. The first M5 movement slice adds a visible placeholder
player with capsule collision and a third-person camera: WASD/left stick
move, RMB-drag/right stick look. See [player controls](docs/player-controls.md).
The next major priority remains the complete locked room, including a
computer/terminal puzzle that opens the exit without requiring an LLM.
See the [roadmap](docs/roadmap.md).

## Prerequisites

- **Clang 19+** or **GCC 15+** (C++23 modules through CMake
  `FILE_SET cxx_modules`). MSVC is tracking but not validated.
- **CMake 3.30+**, **Ninja**.
- **Conan 2.x**, **Python 3.10+**.
- **Linux X11/Wayland dev libs**: glfw is built with both backends. The
  X11 side pulls `xorg/system`, which probes pkg-config for `libxres`,
  `libxcb`, `libxcursor`, `libxinerama`, `libxrandr`, `libxi`,
  `libxkbcommon`; the Wayland side builds libwayland and xkbcommon from
  source but needs the system `xkeyboard-config` data. On Arch:
  `sudo pacman -S libxres libxcb libxcursor libxinerama libxrandr libxi libxkbcommon xkeyboard-config`.
  Conan profiles ship with `tools.system.package_manager:mode=report` so
  Conan never runs `pacman`/`apt` itself — install the libs manually.

## Quickstart

```sh
make bootstrap     # conan install for the default preset (debug)
make configure     # cmake --preset debug
make build         # cmake --build --preset debug
make test          # ctest --preset debug
make shaders       # compile registered shaders through bgfx shaderc
make apps          # list the apps under apps/
make gorden        # build only gorden, then run it
make shaderlab     # build only shaderlab, then run it
make editor        # build and run the scene editor
make gorden ARGS="--scene /absolute/path/to/scene.json"
make run-<app>     # run without building (make run APP=<app> also works)
OPENAI_API_KEY=sk-... make gorden   # robot with a real LLM (gpt-4.1-mini by default)
```

To avoid passing the key every time, keep it in `~/.config/roboslop/llm.json`
(`chmod 600` it; the app only reads this file and never shows it to the robot):

```json
{ "apiKey": "sk-...", "model": "gpt-4.1-mini", "baseUrl": "https://api.openai.com/v1" }
```

`model` and `baseUrl` are optional. `OPENAI_API_KEY`, `GORDEN_MODEL` and
`OPENAI_BASE_URL` override the file when set.

Other presets: `make build PRESET=release|relwithdebinfo|asan-ubsan|tsan`.
The `tsan` preset uses a dedicated Conan profile that rebuilds the entire
dependency graph with `-fsanitize=thread` — first run takes 10–20 minutes
and bifurcates the Conan cache.

`make help` lists every target.

## Documentation

- [Architecture](docs/architecture.md) — engine/app split, working
  principle, subsystem map, AI direction.
- [Roadmap](docs/roadmap.md) — milestones and open questions.
- [Build system](docs/build-system.md)
- [Models from Blender](docs/models.md) — export checklist and loader contract.
- [Decisions log](docs/decisions.md) — append-only history of choices.
- [Commit conventions](docs/conventions/commits.md)
- [Code-style conventions](docs/conventions/code-style.md)
- [Original repo-skeleton brief](docs/prompts/initial-repo-skeleton.md)
  (historical; predates the Roboslop repositioning)

## Licence

All rights reserved — licence to be decided. See [`LICENSE`](LICENSE).
