# Architecture

A structural overview for orientation, not a specification. The opening
sections describe **what exists today**; later sections distinguish
**accepted direction** for playable Gorden and its agents from **likely
future direction**. The first player movement slice is implemented; the room
puzzle is still planned. Anything marked *open question* is deliberately unsettled.

## What Roboslop is

Roboslop is a platform, not a single game's engine. It hosts:

- a real-time rendering / game / simulation core (`engine/`),
- applications built on that core (`apps/`), of which Gorden is the
  first,
- tools around the engine (Shader Lab and a first scene editor exist),
- graphical experiments, and eventually adjacent libraries or "fusion
  projects" that reuse parts of the core.

The core is usable as a general game engine, but it is not designed as
an abstract universal engine. See the working principle below.

## Working principle: application-driven engine development

We work as a variant of "game-driven engine development". Because
Roboslop has several consumers rather than one game, we call it
**application-driven**:

> Do not build general engine abstractions because game engines
> "usually need them". Let concrete programs and experiments create the
> requirements.

In practice:

1. A concrete program needs a capability.
2. Implement the smallest good solution that makes *that program*
   useful. It may live in the app.
3. When the same concept shows up in several consumers, identify the
   shared abstraction.
4. Move it into the engine layer once the boundary has become real.

We specifically avoid building large generic subsystems up front for
hypothetical future needs. Roboslop generalises through concrete use,
not speculation. Corollary: a subsystem that exists in the engine today
is there because Gorden's demo scene needed it, and it is as small as
that need allowed.

## Repository layout

```text
/
├── engine/          # the roboslop library: src/, tests/, CMakeLists.txt
├── apps/
│   └── gorden/      # Gorden: game, gameplay/AI sandbox, engine demo
├── docs/
├── cmake/           # CMake helpers (modules, warnings, sanitisers, shaders)
├── conan/           # Conan profiles
├── third_party/     # FetchContent deps not on Conan Center
└── CMakeLists.txt
```

- **`engine/`** builds the static library target `roboslop`. Its public
  surface is its exported C++23 modules (`roboslop.*`), all in the
  `roboslop` namespace. The engine knows nothing about any particular
  app.
- **`apps/<name>/`** is one executable each, linking `roboslop`. App
  code owns everything specific to that app: scene content, gameplay
  rules, robot personalities, UI. New app directories are created when
  work on the app starts.
- Compiled shaders land at `build/<preset>/assets/shaders/<backend>/`;
  apps pass `assetRoot = "assets"` and `make run` executes from the
  build root so the relative path resolves.

## The applications

### Gorden

A single-player 3D game in development, with a robot companion whose
high-level behaviour can be decided by an AI/LLM. It also serves as our
gameplay/AI sandbox and the debug/demo app where engine features are
tried first. The next major slice is an actual playable room with a
third-person player, described below; the game must remain fully
playable without an LLM.

**Current state.** Gorden loads the shared scene document through
`SceneRuntime`: an enclosed room blockout with a terminal desk, computer,
chair, power unit, central ceiling light and a primitive blocked exit. The
visible fixture has a colocated point light with finite range, while a
directional light provides broad fill; emissive materials are not implemented.
Neither light casts shadows yet. Static scene objects have rigid-body physics.
In `apps/gorden/src/app/main.cpp`,
a separate visible player uses the static human `player.glb`, Jolt capsule
collision and camera-relative keyboard/gamepad movement. `gorden.player` owns the controller and a simple
third-person orbit camera with a sphere sweep for obstructions. The camera
carries the audio listener; `AgentBrain` and saves use the player entity.
See [player controls](player-controls.md) for controls and current limits.
Gorden uses the static two-wheel `gorden.glb`, moving directly toward targets
without a physics body or pathfinding. The terminal and robot chat are developer windows. There
is no gameplay computer, door state, interaction mode or escape puzzle.

The robot perceives structured observations and acts through validated
high-level tools; it never drives locomotion or physics frame by frame.
The agent loop (M2) and memory/save slice (part of M3) exist today.

The gameplay and agent code share the `gorden_agent` module library
(`apps/gorden/src/gameplay/` and `src/agent/`, tested by `gorden_tests`):

- `gorden.player` — player input, capsule movement, orbit and camera obstruction.
- `gorden.agent.observation` — `Named` component, `buildObservation`
  (every named entity within a radius, sorted by distance; no
  line-of-sight yet) and `observationToJson`, the text the model reads.
  Active goals and beliefs are filled in by the brain, not by
  `buildObservation`.
- `gorden.agent.memory` — the long-term memory: episodes, beliefs with
  provenance, goals, keyword retrieval and JSON serialisation. Only the
  validated tools write to it. See [agent memory](agent-memory.md).
- `gorden.save` — capture and restore: the scene objects' transforms go
  in the engine's save game, the robot, the player and the memory in its
  `app` payload. Loading rebuilds the scene through `SceneRuntime` so
  the physics bodies follow; the player controller is reset at the restored
  transform, discarding velocity and cached contacts.
- `gorden.agent.tools` — the tool schemas, `parseToolCall` (JSON →
  `MoveTo` / `Inspect` / `Say` / `Remember` / `Recall` / `Believe` /
  `SetGoal` / `CloseGoal`), and `validate`, the boundary that turns a
  proposal into a `Command` or a rejection with the rule named.
- `gorden.agent.robot` — `RobotMotion` and the kinematic
  `robotLocomotion` system (straight line on XZ, no physics body).
- `gorden.agent.brain` — `AgentBrain`: an event queue (player message,
  tool rejected, move completed, inspect result), a bounded working
  memory, the long-term `AgentMemory`, one in-flight `AsyncCompletion`,
  and the validated action log.
  Events are the only trigger for a think; chained thinks are capped
  per player message; provider errors are logged and never retried on
  their own.

The provider is chosen at startup from `gorden.llm_config`: an API key
in `configDir()/llm.json` (`apiKey`, optional `model` / `baseUrl`) or in
`OPENAI_API_KEY` → the OpenAI-compatible backend, otherwise the scripted
demo. Environment variables (`OPENAI_API_KEY`, `GORDEN_MODEL`,
`OPENAI_BASE_URL`) override the file. The file is read once, never
written by the app, and never mounted into the robot's filesystem.

`gorden.settings` holds what the player can change — player name, robot
name (default "Gorden"), which dev windows are open — as JSON at
`configDir()/gorden.json`. Names feed the `Named` components and the
brain's system prompt (`{robot}` / `{player}` placeholders); the
Settings window applies them live and saves them. Window visibility is
saved whenever it changes; the ImGui layout lives in
`configDir()/gorden.imgui.ini`.

The "Terminal" window is a `TerminalWindow` over a `Shell` over the
app's `Vfs`, which mounts:

| Path | Backing |
|---|---|
| `/var/log/agent.log` | live, read-only: the validated action log |
| `/proc/gorden/observation` | live: `observationToJson` of a fresh observation |
| `/proc/gorden/transcript`, `/proc/gorden/status` | live: chat lines; provider/state/thinks |
| `/proc/gorden/memory` | live, read-only: episodes, beliefs and goals as JSON |
| `/etc/gorden/settings.json` | live, writable: reads the settings, a write applies and saves them |
| `/home/<player>`, `/tmp` | in-memory, writable |
| `/persist` | host mount → `dataDir()/gorden/`, survives restarts |

### Shader Lab (M1, first slice done)

A Shadertoy-like live shader environment that is not limited to a
fullscreen quad. Today (`apps/shaderlab/`): a UV sphere and a plane
rendered with the lab shader pair, a free-fly camera, and a dev-UI
panel. The app polls the shader sources on disk; on a change it runs
`shaderc` on a worker thread through `roboslop.assets.shader_compiler`,
then on the render thread builds the program from the compiled bytes,
swaps it into the `AssetCache`, and rebinds the handles held by
entities. A failed compile leaves the previous program live and shows
shaderc's diagnostics in the panel. Engine pieces this slice pulled in:
`roboslop.platform.process`, `roboslop.assets.shader_compiler`,
`makeProgram` / `replaceProgram` / `rebindProgram`,
`roboslop.render.primitives`, `roboslop.ui`, and `PassCtx::assets`.

Still to come for Shader Lab: imported meshes, terrain, fullscreen
passes, several geometry types side by side, and a real asset identity
once a second consumer needs it.

### Level Editor (first slice, M4)

A separate program under `apps/editor/` creates primitive scenes, selects and
transforms objects, edits solid materials and a directional light, and provides
undo/redo, JSON save/load and physics preview. `roboslop.scene.document` owns
validated authored data; `roboslop.scene.runtime` instantiates that data into
entities and owns shared primitive buffers/material textures. Play creates
physics bodies, Stop removes them and reinstantiates the document. Gorden reads
the same format and adds its player/robot logic. See [scene editing](scene-editor.md).

## Engine: current state

### Entry point and hooks

`roboslop::App` (module `roboslop.app`) is the bridge between an app and
the engine. An app constructs `App::make(AppConfig{...})` and calls
`run()`. `App` owns the window, render context, asset cache, world,
clock, scheduler, per-frame `FrameArena`, fixed-step `SystemGraph`,
`RenderGraph`, `JoltWorld`, and `AudioDevice`, and drives the frame loop
until the window closes.

App code participates through two callbacks on `AppConfig`:

| Hook | When | Signature |
|---|---|---|
| `onSetup` | Once, after init, before graph build | `Result<void>(World&, AssetCache&)` |
| `onBuildGraphs` | Once, after `onSetup`, before the loop | `void(SystemGraph&, RenderGraph&, FrameArena&)` |

`AppConfig::enableDevUi` asks `App` to create a `DevUi` (see "Dev UI"
below) and install it into the world before `onSetup` runs.

`onSetup` seeds entities and loads assets. `onBuildGraphs` declares the
*systems* that run during fixed update and the *passes* that run during
render; there is no per-frame callback. Every frame the engine:

1. resets the arena;
2. polls window events and feeds `Input` a fresh `InputSnapshot`;
3. runs the fixed `SystemGraph` N times at the configured rate via the
   Taskflow executor;
4. runs `RenderGraph::execute` between `RenderContext::beginFrame()` and
   `endFrame()`, sequentially on the bgfx API thread. Each pass receives
   a `PassCtx` with its view id, the viewport, the `World`, the
   `RenderContext`, and the App-owned `AssetCache`.

### Frame loop

Semi-fixed timestep (Fiedler-style accumulator) in `roboslop.time.clock`:
default 60 Hz fixed rate, frames clamped at 0.25 s, leftover fraction
exposed as `alpha()` for render-time interpolation.

### Scheduling

`roboslop.sched`. A `SystemDesc` declares the resource ids it reads and
writes (opaque strings such as `"transforms"`, `"physicsState"`). The
scheduler derives an add-order-forward DAG from those declarations,
materialises it into one `tf::Taskflow` at compile time, and reuses it
every frame with no per-frame allocation. Subsystems that need
engine-owned state (Jolt world, audio device, light uniforms) park a
pointer in `entt::registry::ctx()` rather than adding typed fields to
`SystemCtx`, so `roboslop.sched` has no dependency on any subsystem.

### Platform

`roboslop.platform.window` and `roboslop.platform.input` wrap GLFW.
Snapshots include focus and the first normalized gamepad (sticks and B),
with radial stick deadzone filtering available as a pure helper.
`Input::takeLookDelta` consumes accumulated RMB motion for Gorden once per
fixed update, preserving frames with no tick without duplicating catch-up input.
GLFW may only be called from the thread that owns the window, so `Input`
holds no window handle: `capturePlatformInput(window)` reads the frame's
`InputSnapshot` on that thread and `Input::beginFrame` is fed the result.
`setCursorCaptured` only records a request — free-fly camera control runs
as a fixed system on a scheduler worker — and `Input::applyCursorRequest`
performs the GLFW call once per frame from the App loop.
`roboslop.platform.process` runs a child process to completion and
captures its merged stdout/stderr (`runProcess`); it exists so tools such
as `shaderc` can be shelled out to from a worker thread. POSIX only —
other platforms get a `ProcessError::Unsupported` result rather than a
build break. `roboslop.platform.http` is a blocking HTTP(S) request over
libcurl (`httpRequest`), also meant for worker threads; transport
failures are `Error`s, HTTP status codes are data.

### Rendering

- `roboslop.render.context` owns bgfx init/shutdown and the frame. On
  Wayland it calls `bgfx::renderFrame()` before `bgfx::init`, which runs
  bgfx single-threaded: NVIDIA's WSI loses the `VkSurfaceKHR` when GLFW
  commits the `wl_surface` from the main thread (it does so on every
  configure — resize, fullscreen, focus change) while the render thread
  presents. Other platforms keep the render thread.
- `roboslop.render.graph`: `PassDesc` + `RenderGraph`, same
  conflict-edge rule as systems, dense bgfx view-ID per pass.
- `roboslop.render.frontend`: `FrameArena` (single allocation,
  bump-pointer, reset per frame), flat `DrawItem`s, a packed 64-bit sort
  key (view class, view id, program, depth), and `collectMeshDraws` /
  `sortDraws` / `submitDraws`. No heap allocation in the render loop.
- `roboslop.render.camera`: `Camera` component with a
  perspective/orthographic variant, `ActiveCamera` tag, view/projection
  pushed through bgfx's per-view transforms.
- `roboslop.render.free_fly_camera`: debug camera as a component plus a
  pure tick function. The system captures the cursor as a *level* of
  the right mouse button (held = captured, up = free) so a frame with
  zero fixed steps cannot lose a release; `allowCapture=false` (passed
  while a dev-UI window wants the mouse) stops a hold that started on a
  panel from becoming a fly.
- `roboslop.render.lighting`: one `DirectionalLight`, one optional finite-range
  `PointLight`, and their Lambert terms in the textured fragment shader.
- `roboslop.render.shader`, `roboslop.render.mesh`,
  `roboslop.render.material`: program creation from compiled blobs
  (`makeProgram`) or from the asset root (`loadProgram`), static mesh
  creation with two vertex layouts, and a POD `Material` (program +
  albedo + sampler) the frontend reads per entity.
- `roboslop.render.model`: `ModelInstance`, a list of `ModelDrawPart`s
  (mesh + material + local matrix) on one entity. The frontend emits one
  draw per part with `entity transform * local`, so an imported
  multi-part model moves, collides and is picked as a single entity.
- `roboslop.render.primitives`: procedural UV-sphere and plane
  generators (`sphereGeometry`, `planeGeometry`) producing pos/normal/uv
  `Geometry`, plus `makeGeometryMesh` to upload one as a static `Mesh`.
- Hot replacement of a program is a two-step swap on the render thread:
  `AssetCache::replaceProgram` exchanges the owning `Program` (bgfx
  defers the old handle's release to the end of the frame), then
  `rebindProgram` rewrites the handle copies held in `Mesh` / `Material`
  / `ModelInstance` components. Components keep storing raw bgfx handles; there is no
  asset-identity indirection yet.

### Assets

`roboslop.assets.mesh` wraps Assimp (`loadModelFile`: every mesh in a
glTF file as parts with node transforms, plus materials with base colour
and packed textures — see [models](models.md)),
`roboslop.assets.texture` wraps stb_image (`loadTexture2D`), and
`roboslop.assets.shader_compiler` wraps the `shaderc` executable
(`ShaderCompiler::compile`) for runtime recompilation — it spawns the
binary through `roboslop.platform.process` and returns the compiled blob
or shaderc's diagnostics text. `roboslop.core.file` holds the shared
whole-file reader.
`roboslop.render.asset_cache` caches programs, textures, samplers, and
uniforms and destroys them before bgfx shutdown. There is no asset
identity beyond file paths and no automatic invalidation; a caller that
recompiles a shader swaps it in explicitly via `replaceProgram`.

### Physics

`roboslop.physics` + `roboslop.physics.components`: a `JoltWorld` owned
by `App` and three fixed-update systems (`physicsSpawn`, `physicsStep`,
`syncPhysicsToTransform`) registered with one call. Bodies are described
by `BodyDesc` (sphere/box, static/dynamic). An ECS `Transform` denotes the
authored shape origin; physics sync converts Jolt's centre-of-mass pose back
to that origin, including for offset model-bounds colliders. Jolt runs a
single-threaded job system so Taskflow is the only thread pool in the process.

### Audio

`roboslop.audio.device` owns one miniaudio engine; `roboslop.audio`
provides `AudioListener` / `AudioSource` components and the two systems
that update them. No sound source is spawned in the demo yet.

### Animation

`roboslop.animation.skeleton`, `.clip`, `.state`: skeleton and bone
transforms, per-channel keyframe tracks with heap-free `sampleClip`, and
an `AnimationState` component with a tick system. GPU skinning and rig
extraction from Assimp are not implemented.

### Dev UI

`roboslop.ui` wraps one Dear ImGui context. Input comes through ImGui's
own GLFW platform backend (compiled from the Conan package's
`res/bindings`, chained onto the existing GLFW callbacks); drawing goes
through a minimal bgfx renderer of our own
(`engine/src/ui/imgui_bgfx_renderer.cpp`: transient buffers, per-view
ortho transform, scissor, alpha blend, font atlas). The engine draws no
widgets of its own, but it owns the **window registry**: an app calls
`registerWindow(DevWindow{id, title, draw, visible})` once (in
`onSetup`, where `devUi(world)` is already installed) and, in its last
render pass, `beginFrame()` → `drawWindows()` → `endFrame(viewId)`.
`drawWindows()` draws the main menu bar with a View menu (one checkbox
per window, Show all / Hide all, Hide overlay) and `Begin`/`End` around
every visible window's `draw` callback. F1 toggles the whole overlay
(`App` calls `toggleEnabled()`); `visibility()` / `applyVisibility()`
let an app persist which windows are open. `AppConfig::devUiIniPath`
gives ImGui an ini file so docking layouts survive restarts.
`wantCaptureMouse()` / `wantCaptureKeyboard()` let gameplay or camera
systems yield input to the UI. The ImGui shader pair lives under
`engine/assets/shaders/` and compiles into the shared
`<build>/assets/shaders/<backend>/` tree.

### LLM runtime

`roboslop.llm` is the backend-neutral vocabulary (`ChatRequest`,
`ChatMessage`, `ToolSpec`, `ToolCall`, `ChatResponse`) plus the
`Provider` interface and `startCompletion`, which runs a provider on a
worker thread and hands back an `AsyncCompletion` to poll from the frame
loop. Tool-call arguments stay JSON text: parsing and validating them is
the application's job. `roboslop.llm.openai_wire` is the pure
translation to and from the OpenAI chat-completions JSON, and
`roboslop.llm.backend` exports two partitions: `:scripted` (queued
responses, records requests; tests and the no-key fallback) and
`:openai` (any OpenAI-compatible server over `roboslop.platform.http`:
OpenAI, a llama.cpp server, Ollama). The engine has no opinion about
prompts, tools, or memory — those live in the app.

### Virtual filesystem

`roboslop.vfs` is an in-memory filesystem for developer tooling: plain
directories and files, **live files** whose contents come from
callbacks (a log, an observation, a settings document — optionally
writable), and **host mounts** that map a subtree to a real directory
on disk so everything under it persists. Paths are normalised before
any mount is consulted, so `..` cannot escape a mount. Apps mount what
they want to expose; the shell and terminal below read it.

`roboslop.shell` is a small zsh-flavoured shell over a `Vfs`: builtins
(`ls cd pwd cat echo mkdir rm touch head tail grep wc tree clear env
export history date uptime help`), `|` pipes between builtins, `>` /
`>>` redirection into the VFS, `;` and `&&` sequencing, `$VAR` / `~`
expansion, history, and command/path completion. `tail -f` returns a
`followPath` that the terminal widget keeps polling. Apps add commands
with `registerCommand`; nothing here executes on the host.
`roboslop.ui.terminal` (`TerminalWindow`) is the ImGui front end: a
scrollback, the prompt, an input with Up/Down history and Tab
completion, and follow mode for `tail -f` (Ctrl+C stops it; Esc is
taken by the app for quit). Drawn from a registered dev window.

### Saved games

`roboslop.scene.savegame` is the counterpart to the scene document: a scene
document is authored data, a save game is one *run* of it. A save refers to its
scene by relative path, carries the transforms of the scene objects that moved,
and hands the application an opaque `app` JSON object for everything the engine
does not model. Saves live under `stateDir()` rather than `dataDir()`. See
[the save format](save-format.md).

### Subsystem map

| Subsystem | Library | State |
|---|---|---|
| ECS | EnTT | in use (`roboslop.ecs` facade) |
| System scheduling | Taskflow | in use |
| Rendering | bgfx (FetchContent) | in use |
| Shader pipeline | bgfx `shaderc` | build-time via CMake; runtime via `roboslop.assets.shader_compiler`, which shells out to the same binary |
| Windowing & input | GLFW | in use |
| Physics | Jolt | in use |
| Math | glm | in use |
| Assets — 3D models | Assimp | in use |
| Assets — textures | stb_image | in use |
| Audio | miniaudio (FetchContent) | in use |
| Logging | spdlog | in use (a handful of call sites) |
| JSON | nlohmann/json | in use (LLM wire format, scenes, saves and settings) |
| HTTP client | libcurl (Conan, OpenSSL) | in use (`roboslop.platform.http`, LLM backends only) |
| Debug UI | Dear ImGui (docking) | in use (`roboslop.ui`): GLFW backend from the Conan package, bgfx renderer in `engine/src/ui/`; `ROBOSLOP_DEV_UI=OFF` makes `App` ignore `enableDevUi` |
| LLM runtime | `roboslop.llm` + OpenAI-compatible HTTP backend | in use (Gorden M2); local-first remains the target |

## Accepted direction: first playable Gorden room

Playability is the next major priority, ahead of further M3 reflection/replay
infrastructure or broad editor work. The player starts in a small locked room
with Gorden, explores, uses a computer to solve a small terminal/system puzzle,
unlocks/opens the exit and leaves:

```text
move/explore → observe → interact → reason / use terminal
    → change world state → progress
```

This loop must work without an LLM or a model-generated solution. Gorden can
perceive and react to progress, but inference is not a prerequisite for it.
Only engine needs exposed by this slice should be built; gameplay concepts
stay in Gorden until multiple consumers establish a shared boundary.

### Player, camera and models

Separate these responsibilities (a conceptual split, not a frozen component API):

| Concept | Responsibility |
|---|---|
| Player | `Transform`, visible avatar/model, movement/controller state, collision and character movement |
| Third-person camera | `Transform`, `Camera`, `ActiveCamera`, follow/orbit controller targeting the player |
| Audio listener | Follows the camera |

The first camera follows the player and orbits in yaw/pitch, with
RMB-drag or right-stick look. Movement is relative to the camera's horizontal
orientation. Keep the free-fly camera for developer/editor use. A complete
camera framework, spring arm or cinematic stack is not required.

Use the existing [`.glb` pipeline](models.md) for distinct player and Gorden
models, replacing the temporary ellipsoid avatar and robot cube.
Both may initially be rigid/static models. Animation and GPU skinning are
not prerequisites; animated characters should create that engine requirement
later. The movement slice uses Jolt `CharacterVirtual` for capsule collision,
floor support and small steps; gameplay policy stays in `gorden.player`.

### Input

`InputSnapshot` contains keyboard/mouse state, focus and GLFW-normalized
gamepad sticks/B, captured on the platform thread. Movement, look and cancel
are implemented; interact and terminal routing remain planned:

| Action | Keyboard/mouse | Xbox-style controller |
|---|---|---|
| Move | WASD | Left stick |
| Look | Mouse | Right stick |
| Interact | E | A |
| Cancel | Escape | B |

A general rebinding/action-map framework waits for a concrete need. Input
routing must distinguish exploration, terminal use and developer UI. Escape/B
now cancel mouse capture and suppress gameplay input while held; Escape no
longer closes Gorden. See [player controls](player-controls.md).
Controller terminal navigation/text entry remains an implementation question,
but the playable loop must be completable with either input scheme.

### Interactions and semantic gameplay state

Game objects should expose meaningful state and possible interactions, with
simulation-side validation authoritative for both human and AI requests:

```text
human input      → interaction request → validation → world action
AI tool proposal → interaction request → same validation → same world action
```

`Interactable`, `Interaction` and `Affordance` are possible vocabulary, not
an agreed API or type hierarchy. Keep the initial representation in Gorden.
For illustration only, not a serialization contract:

| Object | Kind | Relevant state | Affordances |
|---|---|---|---|
| Exit door | door | locked, powered | inspect |
| Computer | terminal | online | use |

The actor's access and the world's current state determine what is valid;
exposing an affordance does not authorize an unconditional state mutation.
The exact targeting, range checks and representation are open implementation
questions to resolve with the room. Richer AI use/interact tools can follow
this first human-playable loop through the same gameplay rules.

### Computer and diegetic terminal

Reuse `Vfs → Shell → TerminalWindow` to give the terminal a gameplay role:

```text
Explore mode → interact with computer → Terminal mode
Explore mode ← cancel                ← Terminal mode
```

In Terminal mode, suspend player locomotion and route keyboard/controller
input to the terminal as appropriate. Cancel returns control to the player.
An ImGui/full-screen overlay is enough initially; rendering onto an in-world
screen is optional later work. The gameplay terminal exists because the
player used a computer in the world, rather than being a permanent debug
panel. Keep the developer terminal/debug UI separately available through
the developer UI; gameplay access must not depend on opening a debug window.

Today's developer VFS mounts are useful infrastructure, not an agreed set
of gameplay-visible files or commands. Select those for the puzzle. Any
agent terminal/shell access must remain sandboxed to game abstractions and
the VFS, never arbitrary host execution; no shell tool exists for agents today.

### First puzzle and persistence

The room needs the player, Gorden, a computer and a locked exit door; the
existing generator or another small subsystem is a possible ingredient.
No puzzle solution is fixed yet. Its success must change real simulation
state, including the physical/visual exit and progression:

```text
terminal action → validated game command → door state changes
    → physical/visual world changes → world event → agent can perceive/react
```

Savegames must persist that progression and restore a consistent door and
world state. The existing save container and Gorden-owned `app` payload
provide the starting point; today's saves contain transforms and agent
memory, not lock/puzzle state. The gameplay state representation, authored
setup and save payload changes remain open; a generic engine puzzle or
interaction subsystem is not implied.

## Accepted direction: AI and agents

M2's agent loop and M3's memory slice exist (see "Gorden" above and the
[roadmap](roadmap.md)). Reflection opportunities beyond the existing event
triggers and replay remain unfinished, and need not precede the playable room.

### Semantic-first perception

The agent primarily receives a **simulation-produced, structured
observation** of the world from the robot's point of view. Rendered
images may be used as multimodal augmentation for backends that accept
them, but they are not the canonical world-state channel.

Exactly which facts a robot may observe is an *open question* for
experiments. We deliberately avoid locking in a broad or god-like
perception model now. Today observations contain names, positions,
distance, robot movement, recent events, the player message, goals and
beliefs. Gameplay should pull in richer semantics: entity kind, relevant
state and available interactions/affordances, subject to perception rules.
Recognizing a locked door is more useful than just locating an entity named
"exit door". This remains filtered semantic perception, not raw unrestricted
world access; range is the only visibility limit implemented today.

### High-level actions

The LLM is a decision maker, not a low-level controller. The
conceptual pipeline is:

```text
World
  ↓
Observation
  ↓
Agent
  ↓
Proposed Tool Call
  ↓
Validation
  ↓
Command / Intent
  ↓
Simulation
  ↓
Events
```

Appropriate action granularity is `moveTo(target)`, `pickUp(entity)`,
`inspect(entity)`, `say(...)`. The engine/simulation owns pathfinding,
locomotion, animation, and physics. `Observation`, `ToolCall`,
`Command`/`Intent`, and `Event` are kept conceptually separate even
while the first implementations are tiny: a tool call is a *proposal*
until validation turns it into a command, and events are what the
simulation reports back, not what the agent asked for.

### Local-first, asynchronous inference

The primary target is local inference. The existing OpenAI-compatible
backend can use local or remote endpoints, but the architecture must not
assume an external API service is always present. Inference is asynchronous
relative to the simulation: the game loop never blocks while the model
thinks. Results arrive as proposed tool calls to be validated on the
simulation side.

### Reflection opportunities

The agent has no fixed "think tick". Today player messages, tool
rejections, move completion and inspect results trigger thinking. The
accepted direction extends these with meaningful world events, goal
completion, memory triggers or a conceptual `ReflectionOpportunity` —
a voluntary chance for internal activity when nothing demands immediate
action. These additional triggers are not implemented yet.

Internal activity is expressed through explicit mechanisms (update goal,
store memory, revise belief, inspect memory) rather than by making
private free text or chain-of-thought persistent gameplay state.

### Directives and NPCs: longer-term direction

Distinguish game/story motivation from goals the agent chooses itself:

```text
Directive / motivation → Goals → Current intention → Validated actions
```

A directive is higher-level game/story truth; the LLM may not casually
remove or rewrite it. The agent can create and retire lower-level goals
beneath it. Today only agent-created goals exist; directives and a separate
intention representation are not implemented. Possible Gorden directives
are "Protect the player", "Help the player escape the facility", and
"Preserve yourself when practical". These are illustrative story choices,
not fixed directive text or a planning algorithm.

The same direction should support friendly, neutral and hostile robots.
A security robot might be directed to prevent unauthorized entities from
leaving Sector 3 and to protect facility infrastructure. An active goal or
directive may later create deliberation opportunities when relevant world
state changes, an action completes or fails, or the agent becomes idle with
an active goal remaining. This extends event/opportunity-driven thinking;
it does not introduce a continuous fixed-rate AI tick.

### Capabilities and additional agents: likely future direction

Integrations may become capability unlocks or story progression: for example,
`camera_access`, `facility_network`, `robot_radio` or `security_override`.
An unlock might expose new information, terminal functionality, interactions
or AI tools, such as surveillance cameras or facility systems. These are
primarily game capabilities, not a reason to design a generic plugin system.
Their representation and unlock mechanics remain open.

`AgentBrain` currently serves one robot/player pair. That is acceptable;
do not refactor it pre-emptively for the locked-room slice. The first genuinely
additional AI-controlled NPC should drive any multi-agent abstraction,
possibly an `AgentRuntime` containing `AgentInstance(Gorden)`,
`AgentInstance(SecurityBot)` and others. Those names are conceptual only.
Each agent would need its own event queue, model activity, memory, directives,
capabilities and action state. Ownership and scheduling details wait for
that concrete second NPC.

### Memory is a first-class concept

The robot's memory is not "chat history" or a token count. We separate:

| Layer | Meaning |
|---|---|
| **World truth** | What the simulator knows to be true. |
| **Perception / observations** | What the robot actually observed. |
| **Working memory** | The bounded active context assembled for one inference call. |
| **Episodic memory** | Events the robot remembers ("Anna asked me at the bridge to find the generator"). |
| **Semantic memory / beliefs** | Facts or opinions the robot holds. May be incomplete, stale, wrong, or based on what an NPC said. |
| **Goals** | The agent's explicit active intentions and sub-goals. |
| **Retrieval** | The mechanism that decides which old memories become relevant again. |

World truth and robot belief are explicitly not the same thing. This
makes memory both AI infrastructure and potential gameplay: future robot
upgrades can be larger episodic memory, better retrieval, better
perception, more tools, or more reflection opportunities.

Gorden implements episodic memory, beliefs and goals in
`gorden.agent.memory` (M3, first slice) — plain vectors with keyword
retrieval, written only through validated tools and stored in the save
game's `app` payload; see [agent memory](agent-memory.md). Whether any
of it belongs in the engine, and whether retrieval eventually needs
embeddings or a vector store, are still *open questions*. Belief provenance
is already stored; conceptually:

```text
belief:
    subject
    predicate
    value
    source
    learned_at
    confidence
```

so the recorded source can help answer "why does the robot believe this?".

### Replay and reproducibility

We want to debug and replay AI-driven sessions. The goal is **not** that
the same initial state makes the LLM produce identical output again;
nondeterminism, and the robot becoming its own thinker, is part of the
point. The goal is to replay the **observed and validated event and
action sequence**, e.g.

```text
RobotObserved(...)
AgentToolCall(moveTo(...))
ToolAccepted
RobotArrived(...)
AgentToolCall(inspect(...))
```

In replay mode, previously accepted actions are fed back into the
simulation without asking the model. For debugging we also want to save
observation, prompt, and model response, but those are analysis data,
not the authoritative replay signal. Full determinism of physics or the
whole engine is not required by this design.

## Out of scope for now

Earlier documents described a "battery / token budget" model, a
provider list including specific remote vendors, and robot
customisation mechanics. These remain possible gameplay ideas but are
not part of the accepted architecture until an app needs them; they are
recorded in the history of [`decisions.md`](decisions.md), not here.
