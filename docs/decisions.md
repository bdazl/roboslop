# Decisions

A running log of design and tooling choices for Roboslop. Newest first.
Each entry stays tight: what was decided, why, and where it lives. If a
choice is later changed, append a new entry that supersedes the old one —
don't edit in place.

Entries dated before 2026-09-05 link to the pre-rename layout
(`roboslop/` is now `engine/`, `gorden/` is now `apps/gorden/`). Those
links are left as written; they are history.

---

## 2026-09-11 — Open the first room's exit through a safety interlock

**Decision.** Implement the first slice of the
[first-room design](gorden-first-room-design.md). `door open` succeeds only
after `interlock verify C-17 blue yellow blue red`; opening releases the
door's static body and slides it 2.4 m into the exit wall. Conduit bay C, a
panel in the gap behind the power unit, carries the tag and relay order, and
`/var/log/interlock.log` points there. The answer is a constant.

The room's semantics live in `gorden.first_room` as a table keyed by scene
id, not in the scene document. Observations show an entity's visible state
(`locked`, `open`); `inspect` returns its detail only within 1 m
horizontally (`Rules::inspectReach`). Opening the door reaches the robot
as a `world_event` through `AgentBrain::perceive`.

**Why.** The puzzle needs the terminal, which knows the procedure, and the
robot, which can reach and read the clue. The gap is narrower than the
player's capsule, and the 1 m reach keeps the robot from reading the panel
through the cabinet. The correct answer is the gate rather than a record of
the robot's inspection. That keeps the rules simple to test, and a player
who already knows the answer loses nothing. Gameplay semantics stay in
Gorden until another consumer needs them in the engine.

**Where.** `gorden.first_room`, `gorden.agent.observation` (`Inspectable`),
`gorden.agent.brain`, `gorden.agent.tools` and
`apps/gorden/assets/scenes/room.json`. Routine cognition, which would make
the loop completable without a model, is the next step.

---

## 2026-09-10 — Separate Gorden gameplay UI from developer windows

**Decision.** Use fixed game overlays for anywhere chat, timed robot subtitles,
the fullscreen computer terminal and the Escape pause/settings menu. Keep
Performance (compact summary plus expandable details), Agent log and a separate
developer terminal behind `--dev`, passed by Gorden's Make run targets. F1 only
toggles developer tools. Reuse the sandbox shell for this interface slice; the
puzzle's commands and world effects remain separate work.

**Why.** Playing the game should not require arranging developer windows. Explicit
Gorden-owned interface modes arbitrate input before fixed updates. Escape backs
out of interactions before opening the menu; only the menu/settings pause all
fixed simulation, with background provider results applied after resume.

**Where.** `gorden.interface`, Gorden's app presentation, `AppConfig::enableGameUi`
and `onFrame`, `AppSimulationState`, and [player controls](player-controls.md).
The first computer target is the live `terminal-monitor` within 2 m, without a
general interaction hierarchy. No save format change is needed for transient UI.

---

## 2026-09-10 — Give the ceiling fixture a finite-range point light

**Decision.** Extend forward Lambert lighting with one optional `PointLight`
alongside the existing directional light. Store its world-space position,
colour, intensity and range in the version 1 scene document; older scenes may
omit it. The default room places it at the ceiling globe, while the directional
light remains as broad fill.

The point term uses a squared smooth falloff to zero at its range. Both light
types have fixed uniforms and the first component of each type wins; a general
light array still waits for a scene that needs several lights of the same type.

**Why.** A visible lamp represented only by the directional light cannot cast
light outward from its location. One point light makes the authored fixture
illuminate nearby floors, walls and props without introducing a general
multi-light renderer. Shadow mapping remains separate work because this pass
does not render or sample shadow maps.

**Where.** `roboslop.render.lighting`, `roboslop.scene.document`,
`roboslop.scene.runtime`, the scene shaders, the editor light controls and
`apps/gorden/assets/scenes/room.json`.

---

## 2026-09-09 — Represent the room light with a ceiling fixture

**Decision.** Add a central ceiling fixture and warm globe to the default room
using scene primitives. Keep the scene's existing warm directional light as
the illumination source; the fixture is its visible in-room representation.

**Why.** The room needs a legible light source now, while the renderer and
scene format deliberately support only one directional Lambert light and no
emissive materials. A primitive fixture completes the blockout without
front-loading positional-light infrastructure.

**Where.** `apps/gorden/assets/scenes/room.json`, with placement coverage in
`apps/gorden/tests/scene_asset_test.cpp` and the current limitation in
[architecture](architecture.md#the-applications).

---

## 2026-09-09 — Use wheel contact as Gorden's actor anchor

**Decision.** Gorden's actor Transform and saved position place the robot's
wheel contact. The imported model shares that ground origin; its local visual
transform changes facing only and has no vertical offset.

**Why.** Movement targets and existing saves already describe ground-plane
coordinates. Retaining the placeholder cube's `-0.5` visual offset after the
room floor moved to `y=0` put the finished robot below the floor. Matching the
actor and model origins grounds both new spawns and existing saves without a
save-format migration.

**Where.** `gorden.robot_visual` and [model origins and
placement](models.md#model-origins-and-placement).

---

## 2026-09-09 — Preserve authored origins across physics sync

**Decision.** ECS transforms continue to represent an object's authored
shape or model origin. `syncPhysicsToTransform` reads Jolt's shape-origin
position rather than its centre-of-mass position when copying a body pose
back to the ECS.

**Why.** Imported models use an offset bounds collider because their origin is
normally at the bottom while the collider centre is halfway up the model.
Jolt stores those bodies at their centre of mass internally; copying that
position directly moved every static floor prop upward by half its height on
the first fixed update.

**Where.** `engine/src/physics/jolt_world.cppm`, with the offset-origin
regression in `engine/tests/physics_test.cpp` and the subsystem contract in
[architecture](architecture.md#physics).

---

## 2026-09-09 — Keep the staged default room in sync with its source

**Decision.** Treat `apps/gorden/assets/scenes/room.json` as the authoritative
default room and copy it unconditionally during CMake configuration. Both
Gorden and the editor continue to load `assets/scenes/room.json` from the build
tree by default; `make gorden` now refreshes that staged file automatically
after the source changes.

**Why.** The previous seed-once rule left an existing build directory on an
obsolete test scene indefinitely, making committed room changes appear not to
load. Build-tree scene edits are now explicitly temporary; authored edits must
be saved to the source scene or another path outside `build/`.

**Where.** `apps/gorden/CMakeLists.txt` and
[scene editing](scene-editor.md#start).

---

## 2026-09-09 — Define room ground by its support surface

**Decision.** The first room's walkable floor surface is world `y=0`. Because
the floor is a centred 0.5 m cube, its transform origin is `y=-0.25`.
Floor-standing props use their exported bottom origin at `y=0`; surface props
derive their Y position from the supporting model's exported bounds. Actor
anchors and their existing visual offsets remain separate gameplay contracts.

**Why.** A primitive origin, an imported model origin, the bottom of model
bounds and an actor anchor are different things. Writing the support-surface
calculation down prevents visually floating props and avoids changing actor or
save semantics while authoring a room.

**Where.** [Models](models.md#model-origins-and-placement), the repository
instructions in `AGENTS.md`, and `apps/gorden/assets/scenes/room.json`.

---

## 2026-09-09 — Block out the first playable room around the terminal

**Decision.** Replace the old physics test arena in Gorden's default scene
with a closed 12 by 12 metre room. Put the independent desk, monitor,
keyboard, tower and chair together as the terminal station; place the power
unit separately; reserve a right-wall aperture for a static primitive exit
door.

**Why.** The scene now makes the planned computer interaction and its
power/door consequence legible before their gameplay code exists. The door
is deliberately a primitive: its visual asset and the puzzle solution remain
open decisions, while the blockout already defines a physical route to test.

**Where.** `apps/gorden/assets/scenes/room.json`, with the current milestone
state in [roadmap](roadmap.md#m5--first-playable-room).

---

## 2026-09-09 — Categorise reusable Gorden props

**Decision.** Store reusable environmental assets below
`apps/gorden/assets/models/props/`, with `lowercase_snake_case` names and
adjacent `.blend` / `.glb` sources. Each prop's origin is centred in its
footprint at ground level. The terminal station composition is discarded in
favour of six independent props: chair, computer tower, desk, keyboard,
monitor and power unit.

**Why.** These objects are reusable individually; a terminal-specific prefix
and a duplicated composition would incorrectly imply a single use. CMake now
stages the model tree recursively and the editor lists nested model paths, so
the category is part of the runtime asset identity.

**Where.** `apps/gorden/assets/models/props/`,
`apps/gorden/CMakeLists.txt`, `apps/editor/src/model.cppm`, and
[models](models.md).

---

## 2026-09-09 — Attach Gorden art to the existing robot entity

**Decision.** Replace the checkerboard cube with `models/gorden.glb` through
`gorden.robot_visual` and the scene runtime's cached `ModelInstance` path.
Remove the app-owned checkerboard texture and cube buffers. Keep the robot's
kinematic movement, identity and saved transform unchanged.

**Compatibility.** Offset wheel contact to local y=-0.5, the old unit cube's
bottom, and rotate the visual from glTF +Z to gameplay -Z. The existing
spawn and saved positions therefore retain their ground clearance without
another save-version change. Wheels and arms are static; robot physics,
terrain following and animation remain later work.

**Validation.** The save smoke test now loads both character models, checks
that their buffers survive current and legacy scene reloads, and moves
Gorden to a target while checking its heading and height. A rendered frame
covers both characters.

---

## 2026-09-09 — First Gorden art: a two-wheel service robot

**Decision.** Author `gorden.blend` and `gorden.glb` as a roughly one-metre
service robot with two wheels, small grippers and a box-shaped head with
two readable eyes. Use light grey, petrol blue and orange to relate it to
the player while giving it a distinct silhouette.

**Why.** The user selected this first design. Wheels suit the current
static model pipeline without requiring a walk cycle. All visible details,
including eyes, use solid base colours supported by the renderer. Keep
editable parts and portrait staging separate and export only the robot.

**Scope.** CMake stages the model for editor use. Replacing the runtime
checkerboard cube remains a separate integration step. Dimensions and
export instructions are in [models](models.md).

---

## 2026-09-09 — Attach player art through the existing model renderer

**Decision.** Replace the ellipsoid with a `ModelInstance` on the existing
player entity. `gorden.player_visual` adapts the model's foot origin and +Z
forward to the controller's centred capsule and -Z forward. Rendering uses
the existing scene shader and authored base colours; movement is unchanged.

**Why.** The player is the second concrete consumer of the scene model
uploader. `SceneRuntime::instantiateModel` exposes its cached resources for
app-owned actors without giving them scene identity or rigid bodies. The
cache already survives scene replacement, so the actor remains drawable
across save/load. No new asset manager or animation system is needed.

**Compatibility.** New Gorden saves use app payload version 2. Version 1
loads discard the old ellipsoid scale but preserve position and rotation.
The engine save envelope and memory format stay unchanged.

**Validation.** The display-dependent save smoke test loads the player,
exercises current and legacy save/load, checks cached buffers, moves and
turns the player, and captures a rendered frame in its temporary output
directory.

---

## 2026-09-09 — First player art: a static low-poly technician

**Decision.** Author a stylised human in blue work overalls with orange
safety bands as `apps/gorden/assets/models/player.blend` and `player.glb`.
Use solid base colours and a relaxed standing pose, without a rig, so the
asset fits the existing model loader. Keep the editable parts and portrait
studio separate and export only the character.

**Why.** The first room needs a recognisable player distinct from Gorden.
The user selected this visual direction; animation is not a prerequisite.
The 1.8 m model uses a foot origin and Blender -Y forward (glTF +Z).
CMake stages it for editor use; replacing the runtime placeholder remains
a separate step. Export details are in [models](models.md).

---

## 2026-09-08 — First M5 slice: player capsule and third-person controls

**Decision.** Implement movement before the room's interaction/puzzle loop.
`gorden.player` owns a Jolt `CharacterVirtual` capsule, camera-relative
movement, orbit camera and a sphere sweep for camera obstruction. Existing
engine input snapshots gain normalized GLFW gamepad sticks/B and focus;
Gorden consumes accumulated mouse displacement once across fixed ticks.
No general camera framework or input rebinding system is introduced.

**Why.** Jolt already supplies collision, support, stairs and wall sliding.
Keeping gameplay policy in Gorden lets the real room drive later engine
abstractions. A separate placeholder avatar makes movement observable
without making character art a prerequisite for this slice. WASD/left stick
move, RMB-drag/right stick look, Escape/B cancel capture, and developer UI
keyboard focus suspends gameplay input. Escape no longer quits Gorden.

**Consequences.** Physics updates before character movement; audio follows
the camera and the brain observes the player entity. Save/load retains the
player transform and clears controller contacts/velocity. Orbit is not
persisted. Authored spawn points, character models, jumping and the actual
locked-room interaction loop remain outside this slice.

**Where.** `apps/gorden/src/gameplay/player.cppm`,
`engine/src/platform/input.cppm`, [player controls](player-controls.md).

---

## 2026-09-08 — Gorden's next major slice is a playable locked room

**Decision.** Prioritize a first playable room (M5) ahead of remaining M3
reflection/replay infrastructure and broad editor expansion. Separate the
visible player and character movement/collision from a simple third-person
camera; support keyboard/mouse and Xbox-style controller input. Existing
`.glb` loading is enough for rigid player and robot models; animation is not
a prerequisite. This supersedes the earlier top-down gameplay direction.

The player enters gameplay terminal mode by interacting with a computer.
A terminal puzzle must change validated simulation state, open the exit,
expose relevant semantics/events to Gorden and persist progression. The
whole game must remain playable without an LLM. Keep developer terminal
access separate, and route human and future AI interactions through the
same authoritative gameplay rules.

**Why.** The existing scene, input, physics, model, terminal and save
foundations are enough to make playability the next concrete source of
engine requirements. Interaction concepts stay Gorden-specific until
multiple consumers establish a shared boundary. No interaction API, puzzle
solution, general camera/input framework or multi-agent runtime is locked
by this decision. Completed milestones and unfinished M3 work are preserved.

**Where.** [Architecture](architecture.md#accepted-direction-first-playable-gorden-room)
and [roadmap](roadmap.md#m5--first-playable-room). This records direction;
no gameplay systems are implemented by this documentation change.

---

## 2026-09-07 — Frame statistics first; the inverted thread model waits

**Decision.** The engine measures itself before it is restructured.
`roboslop.time.frame_stats` collects per-frame CPU, GPU and bgfx
wait timings; the dev UI grew a "Performance" overlay; `ROBOSLOP_BENCH_*`
runs a fixed number of frames and reports percentiles. The planned
inversion of the thread model — main thread pumping `bgfx::renderFrame()`
while a separate app thread calls `bgfx::init` — is designed but **not
built**.

**Why.** The inversion's purpose was to keep bgfx's submit/render
pipelining while putting every Vulkan/WSI call on the thread that owns
the `wl_display`, which is what the 2026-09-07 single-threaded workaround
gave up. The first benchmark says the pipelining is not worth buying back
yet: Gorden's scene (17 draw calls, 2548x1391) runs a whole CPU frame in
0.167 ms at p50 — 0.136 ms render, 0.022 ms fixed step, 0.031 ms GPU — in
a debug build.

bgfx splits a frame into a submit half (recording the command buffer, S)
and a render half (translating it to Vulkan and presenting, D). The
measured render block is S + D together, because single-threaded mode
runs the render half inline; the whole frame is F + S + D, where F is the
fixed step. A render thread overlaps the halves, so the steady-state
frame becomes max(F + S, D) and the win is min(F + S, D) — at best half
the frame, around 0.08 ms, when the two are evenly matched.

That ceiling is a doubling of CPU throughput, and it still does not
matter: at 60 Hz the budget is 16.7 ms and we are using one percent of
it, with vsync sleeping away the rest. The restructuring it would take is
not small: an in-house ImGui platform backend (upstream's supplies text
input, clipboard, nine cursors and the monitor list), a split of `App`'s
ownership across two threads, and a new startup/shutdown handshake with
several deadlock and hot-spin traps.

The run was measured on a hidden compositor workspace, so if presentation
is cheaper there than on a visible one, D — and with it the win — is
understated. Not by the order of magnitude it would take to change the
answer.

**Consequences.**

- Wayland keeps the single-threaded bgfx mode from the entry above. The
  race is still fixed; only the pipelining is still forfeit.
- The design survives in the plan and in this entry, so the work can be
  picked up when a scene actually becomes CPU-bound.
  `waitSubmit`/`waitRender` cannot be the trigger: they measure how long
  each half waits for the other, and are zero by construction while bgfx
  is single-threaded. Watch the `render` block instead — it is exactly
  the S + D a render thread would overlap. At a few milliseconds against
  a 16.7 ms budget, halving it starts to be worth the restructuring.
- One piece landed on its own merits: `Input` no longer calls GLFW.
  `setCursorCaptured` was running `glfwSetInputMode` from a scheduler
  worker, because free-fly camera control is a fixed system.
- The benchmark is configured by environment rather than `AppConfig`;
  see `docs/build-system.md`.

**Where.** `engine/src/time/frame_stats.cppm`,
`engine/src/app/benchmark.cppm`, `engine/src/ui/perf_window.cppm`,
`engine/src/platform/input.cppm`.

---

## 2026-09-07 — bgfx runs single-threaded on Wayland

**Decision.** `RenderContext::make` calls `bgfx::renderFrame()` before
`bgfx::init` when GLFW picked the Wayland platform, which puts bgfx in
single-threaded mode. X11, Windows and macOS keep the render thread.

**Why.** On Hyprland with the NVIDIA driver the app lost its
`VkSurfaceKHR` (`vkQueuePresentKHR` → `VK_ERROR_SURFACE_LOST_KHR`, then
`vkCreateSurfaceKHR` refusing the same `wl_surface` with "presentation to
the given surface not supported") and went permanently black while the
process stayed alive. The trigger is any `xdg_surface` configure —
resize, fullscreen, or a plain focus change — because GLFW answers each
one with a `wl_surface_commit` from the main thread while bgfx's render
thread is presenting. NVIDIA's WSI does not survive that race; a
single-threaded bgfx puts every Vulkan call on the thread that owns the
window and removes it. Measured with a fullscreen-toggle stress harness:
multi-threaded lost the surface after 41 and 72 toggles, single-threaded
survived 165 and 164 with none. Compositor-driven resize and fullscreen
stress on a hidden workspace does *not* reproduce the loss even
multi-threaded, so the fix is not re-verifiable that way; the harness
that does reproduce it pulls the window over the desktop.

**Consequences.** Wayland loses bgfx's render-thread parallelism; the
submit/render split still exists in the API, it just runs inline. The
`third_party/patches/` swapchain patch stays — it is what turns a
surviving loss into a trace line rather than a crash.

**Where.** `engine/src/render/context.cppm`, `docs/architecture.md`.

---

## 2026-09-07 — Agent memory in the app, a save game in the engine

**Decision.** M3's memory slice splits in two. `gorden.agent.memory` (in the
app) owns episodes, beliefs with provenance and goals; `roboslop.scene.savegame`
(in the engine) owns a versioned save container that refers to a scene, stores
the transforms of its objects, and hands the application an opaque `app` JSON
object for everything else.

**Why.** The memory model is one application's guess at what an agent needs;
nothing else consumes it yet, so it stays where the application-driven principle
puts it. Persistence, on the other hand, is not agent-specific — any app will
want to store a run — and the user asked for a general save state rather than a
memory file. The `app` payload is what keeps the engine format from growing a
key per application: Gorden versions `{robot, player, sim_time, memory}` itself.

**Consequences.**

- Writes to memory only happen through validated tools (`remember`, `recall`,
  `believe`, `setGoal`, `closeGoal`), so the robot chooses what to keep while
  the rules still decide what is allowed. The simulation never writes behind the
  model's back.
- Active goals and beliefs are rendered into every observation; episodes reach
  the model only through `recall`. Retrieval is word matching with a recency
  tie-break — no embeddings until a concrete failure asks for them.
- Saves live under a new `stateDir()` (`$XDG_STATE_HOME/roboslop`), not
  `dataDir()`, which is already the robot's `/persist` mount.
- Saving and loading are explicit (Settings buttons, `save` / `load` in the
  terminal) and are carried out in the render pass, the only place with an
  `AssetCache`. Loading rebuilds the scene through `SceneRuntime::replace` with
  the saved transforms applied to the authored document, so physics bodies end
  up where the objects are; scene objects the save no longer matches are
  ignored rather than treated as an error.
- `apps/gorden/tests/save_smoke.cpp` covers the load path, which needs a render
  context, the way the editor's runtime smoke test does. `gorden.settings` grew
  `settingsFromJsonText` so `main.cpp` no longer includes `nlohmann/json.hpp`:
  including it next to modules that export `nlohmann::json` types breaks the
  build with ODR errors in libstdc++ headers.

## 2026-09-07 — Trim the clang-tidy check set to what this codebase can honour

**Decision.** `make tidy` is now warning-free. Getting there fixed ~700
findings in the code and disabled the checks below, each because it fights
a deliberate property of this codebase rather than finding a defect.

Bounds and varargs, the game-dev pragmatics already behind
`pro-bounds-pointer-arithmetic`:

- `cppcoreguidelines-pro-bounds-avoid-unchecked-container-access` and
  `-pro-bounds-constant-array-index`: indexing a `glm::vec3` or a
  three-element table by axis is how graphics code reads.
- `cppcoreguidelines-pro-type-vararg`: Dear ImGui's `Text` family and
  `snprintf` are variadic by design.
- `modernize-avoid-c-arrays` (and its alias): GPU vertex layouts and
  third-party interface implementations need C array members.

Checks that contradict the compiler or another check:

- `readability-redundant-member-init`: dropping `{}` from a member removes
  the default member initializer that lets `-Wmissing-designated-field-initializers`
  accept partial designated initialisers, which this codebase uses widely.
- `misc-use-anonymous-namespace`: it wants the opposite of
  `misc-use-internal-linkage` for module interface units, which it treats
  as headers. Module-local helpers are marked `static` instead.
- `modernize-redundant-void-arg` and `misc-static-assert`: both misparse
  module code — the first reads `(void)x;` as a declaration, the second
  proposes `static_assert` for runtime expressions, which does not compile.
- `readability-redundant-declaration`: flags Jolt's `operator new` overloads
  and a `module :private` definition, and its fix deletes the definition.

Domain facts:

- `performance-enum-size`: the error enums are `: int` to mirror
  `Error::code`, and none of these enums is stored in bulk.
- `performance-no-int-to-ptr`: the frame arena and native window handles
  do exactly this, deliberately.
- `concurrency-mt-unsafe`: `getenv`/`strerror` on startup and in the POSIX
  process wrapper, with no portable thread-safe replacement in use.
- `bugprone-exception-escape`: `main` is allowed to terminate on an
  exception; the apps report through `Result` everywhere else.
- `cppcoreguidelines-avoid-const-or-ref-data-members`: `CommandContext`
  holds the shell and VFS by reference on purpose.
- `bugprone-unused-return-value` keeps its default but gains
  `AllowCastToVoid`: a `(void)` cast is how this codebase says the failure
  is acceptable here.

**Where.** [`.clang-tidy`](../.clang-tidy),
[`docs/conventions/code-style.md`](conventions/code-style.md).

## 2026-09-07 — Disable bugprone-unchecked-optional-access

**Decision.** `.clang-tidy` disables `bugprone-unchecked-optional-access`.

**Why.** clang-tidy 22.1.8 segfaults in the check's flow-sensitive
analysis (`RecordStorageLocation::getChild` from `VisitMemberExpr`) whenever
a function that touches a `std::optional` also reads a glm anonymous-union
member such as `position.x`, in a translation unit that imports C++20
modules whose global module fragments include overlapping glm headers.
`apps/editor/src/main.cpp` hits this in `drawInspector` and `drawGizmo`,
which took the whole `make tidy` run down. The same code passes without
module imports, so it is a clang bug, not a code problem. Minimal trigger
with the editor's compile command:

```cpp
import roboslop.scene.transform;
import roboslop.render.lighting;
#include <glm/vec3.hpp>
#include <optional>
void probe(std::optional<int>& t, glm::vec3& v) { v.x += 1; if (!t) {} }
```

Re-enable when a clang release runs it cleanly over `apps/editor`.

**Where.** [`.clang-tidy`](../.clang-tidy),
[`docs/conventions/code-style.md`](conventions/code-style.md).

## 2026-09-07 — Models enter scenes as a path on the object, drawn by one entity

**Decision.** A scene object references a model file with
`"geometry": "model"` and a `model` path relative to the asset root
(`models/crate.glb`); primitives omit the key so existing files are
unchanged. There is no asset table: the path is the identity, and
`validateScene` only requires it to be relative and free of `..`. The
object's `material` stays required but is ignored for models, which draw
with the materials inside the file (packed or adjacent texture, else a 1×1
base-colour texture).

`SceneRuntime` uploads each referenced file once (`loadModelFile`, one
static mesh per part, one texture per material, `modelBounds`) and gives
the object a single entity carrying the new `ModelInstance` component
(`roboslop.render.model`): a list of parts with mesh, material and a local
matrix that the frontend composes with the entity transform. The collider
is a `BoxShape` around the model bounds, scaled by the object, with the new
`center` offset because authored models are rarely centred on their origin.
The editor lists `assets/models/*.glb` as add buttons, picks models by a
ray/AABB test against the runtime's bounds, and draws the selection box
around those bounds. Gorden stages `crate.glb` with an unconditional
`configure_file` and places it in `room.json`.

**Why one entity.** `syncPhysicsToTransform` writes back only the body's
own entity. One entity per part would need a parent/child hierarchy or a
follower system for a dynamic model to fall as one piece; a multi-part
draw component keeps physics, picking and `SceneIdentity` on one entity
without introducing transform propagation before anything else needs it.

**Why no asset table yet.** A `models` table with ids would settle the
asset-identity question early, but only one consumer exists and the path
already round-trips. Revisit when a second asset type is referenced from
scenes or when renaming files becomes a real chore.

**Not in this slice.** Hierarchy in the ECS, prefabs, per-object material
overrides or tinting for models (would need `fs_scene` to multiply the
base colour), mesh or convex colliders, and a file dialog in the editor.

## 2026-09-07 — Blender models arrive as glTF binaries read through Assimp

**Decision.** Authored models are exported from Blender as `.glb` (glTF 2.0
binary, textures packed, +Y up, metres, modifiers applied) and committed
next to their `.blend` source under `apps/<app>/assets/models/`. The engine
reads them with Assimp through `loadModelFile` in `roboslop.assets.mesh`,
which replaces the first-mesh-only `loadMeshFile`: it walks the node
hierarchy and returns one `ModelPart` per mesh with the node's full
transform and material index, plus materials with base colour and either a
texture path or the packed image bytes. `MeshAsset` indices are now 32-bit;
`makeStaticMesh` gained a matching overload and `loadTexture2D` an
in-memory overload for packed textures.

**Why glTF.** Blender's exporter is Khronos-maintained and ships with
Blender. FBX is proprietary and Blender's support is reverse-engineered,
OBJ has neither hierarchy nor rigs, and `.blend` is unreadable outside
Blender. glTF specifies coordinate system, units, PBR materials, hierarchy,
skins and animation, which is the set Roboslop still has to grow into.

**Why Assimp, for now.** It is already a dependency, and a Blender 5.2
export was verified against the Conan build: hierarchy, per-material mesh
split, node scale and rotation, packed PNG, base colours and the Y-up
conversion all arrive intact. The loader's `aiProcess_FlipUVs` restores
glTF's top-left UV origin after Assimp's import flip (checked against the
raw accessor data). Known weak spots — slower parsing, dropped PBR
extensions, a history of skinning bugs — are tolerable for static props.
If they bite once animation import starts, fastgltf or cgltf (both on
Conan Center) replace the module behind the same `ModelAsset` API.

**Validation.** `engine/tests/model_loader_test.cpp` loads the checked-in
`engine/tests/assets/crate.glb` (generated by `make_fixture.py`) and checks
parts, transforms, materials, the packed texture and the UV origin; a
generated 67600-vertex grid checks 32-bit indices; missing and unreadable
files are errors. [`docs/models.md`](models.md) holds the export checklist.

**Not in this slice.** Scene-format references to model files, rig
extraction, PBR channels beyond base colour, and staging models into
`build/` (added with the first app model).

## 2026-09-06 — Author primitive scenes before agent memory

**Decision.** Pull a first M4 editor slice ahead of M3. The user needs to build
environments for Gorden now. A separate `apps/editor` shares a versioned JSON
document and scene instantiation with Gorden. Authored data owns stable object
and material IDs; transient ECS, GPU and Jolt handles stay in the runtime.
Play/Stop reinstantiates the authored document and releases old physics bodies.

**Scope.** Flat scenes with cubes, spheres, planes, solid materials and one
directional light. A small CPU picking and projected-axis manipulation layer
uses the existing GLM/ImGui dependencies. Translation/rotation use world axes;
scale uses local axes. History stores up to 128 document snapshots and groups
continuous drags. Imported model hierarchies, prefabs and terrain wait for a
subsequent use case. Primitive winding is outward CCW; the previous app-local
cube arrays were wound inward and have been removed in favor of `cubeGeometry`.

**Validation.** Document round trips and rejection, primitive winding, picking,
undo/redo, physics removal, and a separate display-dependent runtime smoke
executable. See `docs/scene-editor.md` for the workflow and current limits.

## 2026-09-06 — bgfx is patched to survive a lost surface during swapchain creation

**Decision.** `third_party/patches/` holds git patches applied to the
bgfx checkout by `FetchContent`'s `PATCH_COMMAND`. The first one makes
`SwapChainVK::update()` treat `VK_ERROR_SURFACE_LOST_KHR` from
`vkCreateSwapchainKHR` like the same error from acquire/present: flag
the surface and swapchain for recreation and return, instead of
tripping the fatal `VK_CHECK`.

**Why.** Gorden died after a while on Hyprland with the NVIDIA driver:
a resolution update recreated the swapchain and the WSI answered
`SURFACE_LOST`, which bgfx only handles when it comes from acquire or
present. The trigger has not been reproduced; the patch removes the
hard crash so the next occurrence leaves a trace line and a recovered
window rather than a core dump. Patching beats forking: the change is
one hunk, bgfx.cmake stays pinned, and the patch is dropped once it
lands upstream.

**Where.** `third_party/patches/`, `third_party/CMakeLists.txt`.

---

## 2026-09-06 — Native Wayland through GLFW's runtime platform selection

**Decision.** The Conan glfw package is built with both Linux backends
and GLFW picks one at init (Wayland when `WAYLAND_DISPLAY` is set, X11
otherwise). `Window::nativeHandles()` asks `glfwGetPlatform()` and hands
bgfx a `wl_display`/`wl_surface` pair or an X11 display/window
accordingly; `RenderContext` already tagged the Wayland handle type.
The Wayland `app_id` is set from the window title so compositor rules
see the same identity X11's `WM_CLASS` gave. The Conan-built libwayland
and xkbcommon are not linked: GLFW loads them with `dlopen()` by soname,
so the session's own libraries are what run.

**Why.** Running through XWayland meant the app saw XWayland's keymap,
not the compositor's, and anything that rewrites the X keymap (fcitx5's
xcb module, for one) changed the app's layout under it. Native Wayland
reads the keymap the compositor sends. Linking the Conan copies would
have loaded a second libwayland-client next to the one libdecor pulls
from the system, and the Conan xkbcommon looks for Compose tables inside
the Conan cache, which silently disables dead keys. bgfx's Vulkan path
creates the surface with `VK_KHR_wayland_surface`; the OpenGL path
`dlopen()`s libwayland-egl itself, so no bgfx build flag is needed.

**Where.** `conanfile.py` (`configure()`), `engine/CMakeLists.txt`
("GLFW on Linux"), `engine/src/platform/window.cppm`.

---

## 2026-09-06 — The API key lives in its own file, not in the settings document

**Decision.** `gorden.llm_config` reads `configDir()/llm.json`
(`apiKey`, optional `model`, `baseUrl`) at startup; `OPENAI_API_KEY`,
`GORDEN_MODEL` and `OPENAI_BASE_URL` override it. The app never writes
the file and warns if it is group- or world-readable.

**Why.** Typing the key on every launch was the friction. It cannot go
into `gorden.json`: that document is mounted as
`/etc/gorden/settings.json` in the robot's sandbox, so the model could
read and echo its own key. A separate, read-only, unmounted file keeps
the secret out of the agent's reach and out of anything the app
serialises. Environment first so one-off runs and CI need no file.

**Where.** `apps/gorden/src/agent/llm_config.cppm`,
`makeProvider()` in `apps/gorden/src/app/main.cpp`.

---

## 2026-09-05 — Dev windows are registered with the engine; the app only draws contents

**Decision.** `DevUi` owns a window registry: apps call
`registerWindow({id, title, draw, visible})` and `drawWindows()`; the
engine draws the main menu bar (View: per-window checkboxes, Show all,
Hide all, Hide overlay), `Begin`/`End`, and handles F1. Visibility is
exposed for persistence; ImGui's ini file lives in the app's config dir.

**Why.** Every app was about to grow the same "which windows are open"
code, and Shader Lab already had a panel. Keeping `Begin`/`End` in the
registry means a closed window can always be reopened from the menu.

**Where.** `engine/src/ui/dev_ui.cppm`, `AppConfig::devUiIniPath`.

---

## 2026-09-05 — Debug terminal: own widget over an in-memory VFS, not libghostty or a PTY

**Decision.** The terminal is a line-based ImGui widget
(`roboslop.ui.terminal`) over a builtin shell (`roboslop.shell`) over an
in-memory filesystem (`roboslop.vfs`). No VT emulation, no host
processes. The VFS has live files (callbacks) and host mounts (a subtree
backed by a real directory); only host-mounted subtrees persist. Apps
mount what they want to expose; Gorden mounts its agent log,
observation, transcript, status, settings, and `/persist`.

**Why.** The need is inspecting and poking at the running app, not
running real programs: `tail -f` on the agent log, reading the
observation, editing settings. libghostty would add a Zig toolchain and
a young embedding API for capabilities we would not use; a PTY would
defeat the virtual filesystem and the sandbox. Persisting only through
explicit host mounts keeps the default state disposable while giving a
clear place for things that should survive.

**Where.** `engine/src/vfs/`, `engine/src/shell/`,
`engine/src/ui/terminal.cppm`, `mountGordenFiles` in
`apps/gorden/src/app/main.cpp`.

---

## 2026-09-05 — Per-user settings as JSON in the XDG config dir; names are settings

**Decision.** `roboslop.core.paths` resolves `$XDG_CONFIG_HOME/roboslop`
and `$XDG_DATA_HOME/roboslop`; `roboslop.core.json_file` reads and
atomically writes JSON. Gorden keeps player name, robot name (default
"Gorden"), and window visibility in `gorden.json`. Names feed the
`Named` components and the brain's system prompt.

**Why.** Settings must survive `make clean` and be per user, and the
schema is app-owned so the engine only provides paths and file I/O.

**Where.** `engine/src/core/paths.cppm`, `json_file.cppm`,
`apps/gorden/src/agent/settings.cppm`.

---

## 2026-09-05 — First LLM backend is OpenAI-compatible over libcurl, verified against OpenAI

**Decision.** `roboslop.llm.backend:openai` talks to any server that
implements the OpenAI chat-completions API (tools included) through
`roboslop.platform.http`, a blocking libcurl wrapper. The first
verification target is OpenAI itself (`gpt-4.1-mini`, key from
`OPENAI_API_KEY`); a llama.cpp server or Ollama is the same code with
`OPENAI_BASE_URL` changed. libcurl was chosen over cpp-httplib for
TLS out of the box and for future remote backends.

**Why.** No local model server was available on the development
machine, and the API shape is identical, so verifying against OpenAI
first costs nothing architecturally. The local-first direction in
`architecture.md` stands: nothing in the engine assumes a remote
service, and the scripted backend keeps the whole chain runnable and
testable without any server.

**Where.** `engine/src/llm/`, `engine/src/platform/http.cppm`,
`conanfile.py` (`libcurl`).

---

## 2026-09-05 — Agent split: provider in the engine, observation/tools/brain in the app

**Decision.** The engine owns the backend-neutral chat vocabulary, the
`Provider` interface, async completion, the wire format, and the
backends. Everything that gives the robot meaning — what it observes,
which tools exist, how a proposal is validated, how it moves, when it
thinks — is Gorden code (`gorden_agent`).

**Why.** The provider layer is the same for any app that wants a model;
the agent semantics are exactly what the application-driven principle
says not to generalise before a second consumer exists. `gorden_agent`
being a separate module library with its own tests keeps the app's
logic testable without a window.

**Where.** `engine/src/llm/`, `apps/gorden/src/agent/`,
`apps/gorden/tests/`.

---

## 2026-09-05 — Kinematic robot, event-triggered thinks, capped chains, action log

**Decision.** The robot moves kinematically in a straight line on the
XZ plane (`RobotMotion`, no physics body). The brain thinks only when
events arrive (player message, tool rejected, move completed, inspect
result); a `say` and an accepted `moveTo` produce no immediate event. At
most `maxChainedThinks` (4) thinks follow one player message. Provider
errors are logged and shown but never retried automatically. Every
observation delivered, proposal, verdict, and resulting event is
appended to a plain-text validated action log.

**Why.** Straight-line motion is enough to prove the loop and keeps
tests deterministic. Event-only triggers implement the "no think tick"
direction from the start. The cap bounds cost and stops a looping
model. The log is the M3 replay signal in its simplest form.

**Where.** `apps/gorden/src/agent/robot.cppm`, `brain.cppm`.

---

## 2026-09-05 — Runtime shader compilation shells out to the shaderc binary

**Decision.** `roboslop.assets.shader_compiler` runs the `shaderc`
executable that bgfx.cmake already builds (via
`roboslop.platform.process`, a fork/exec wrapper that captures merged
stdout/stderr) rather than linking shaderc into the engine. The
`--profile` is derived from the live bgfx renderer (`shaderProfileFor`),
the `--platform` from the compile-time OS, and the argument list is
built by a pure function so it can be unit-tested against what
`bgfxToolUtils.cmake` generates at build time. Diagnostics are shaderc's
text output, verbatim.

**Why.** Lowest integration cost and it keeps a large compiler (glslang,
SPIRV-Cross, fcpp) out of the process. shaderc's text diagnostics are
good enough for a panel. Linking it in remains possible behind the same
`ShaderCompiler` interface if latency or structured errors are needed.

**Where.** `engine/src/assets/shader_compiler.cppm`,
`engine/src/platform/process.cppm`, `shaderProfileFor` in
`engine/src/render/shader.cppm`.

---

## 2026-09-05 — Program hot swap by handle rebinding, not asset identity

**Decision.** Components keep storing raw `bgfx::ProgramHandle`s. A
hot swap is `makeProgram(bytes)` → `AssetCache::replaceProgram` (which
destroys the old `Program`; bgfx defers the release to end of frame) →
`rebindProgram(world, old, new)` walking `Mesh` and `Material`. No
stable asset id or indirection table is introduced yet.

**Why.** Shader Lab has one program and a handful of entities; an ECS
walk is trivial and leaves the frontend, sort key, and draw path
untouched. A proper asset identity is a cross-cutting design that the
level editor (M4) will need for scenes anyway — that is the right time
to design it with two consumers in hand.

**Where.** `engine/src/render/shader.cppm`, `asset_cache.cppm`,
`frontend.cppm`. Roadmap M1 open question.

---

## 2026-09-05 — Shader sources per app; polling file watcher in the app

**Decision.** Each app keeps its shader sources under its own
`assets/shaders/src/`; the engine's ImGui pair lives under
`engine/assets/shaders/src/`. All compile into one
`<build>/assets/shaders/<backend>/` tree so any app's `assetRoot`
resolves engine and app programs alike. Shader Lab's file watching is
an app-local `ShaderWatcher` that polls `last_write_time` every 250 ms;
the reload state machine (`ShaderReloader`) is app-local too.

**Why.** No two apps share a shader yet, so a top-level `assets/` would
solve a problem nobody has. Polling three files is three `stat` calls
and needs no platform code. Both the watcher and the reloader move into
the engine when a second app wants the same shape (application-driven
principle).

**Where.** `apps/shaderlab/src/lab/`, `engine/CMakeLists.txt` (shader
output dir), `cmake/ShaderCompile.cmake` (`TARGET_VAR`).

---

## 2026-09-05 — Dev UI: ImGui as an engine module, panels drawn by the app

**Decision.** `roboslop.ui` owns one Dear ImGui context and is created
by `App` when `AppConfig::enableDevUi` is set. Input uses ImGui's own
GLFW platform backend compiled from the Conan package's `res/bindings`
(same version as the library, no vendored copy). Rendering uses a
minimal bgfx backend written in-tree (`engine/src/ui/imgui_bgfx_renderer.cpp`)
instead of a third-party `imgui_impl_bgfx`. The engine adds no widgets;
an app draws its panels inside its own last render pass between
`beginFrame()` and `endFrame(viewId)`, and reaches the instance through
the world context like `JoltWorld`. The module is always built;
`ROBOSLOP_DEV_UI=OFF` only makes `App` skip creating it.

**Why.** Shader Lab needs a diagnostics panel, which is the first real
consumer. Keeping ImGui in the engine avoids every app re-implementing
the same glue, while leaving widget content to the app keeps the engine
free of app-specific UI. The bgfx backend is ~150 lines; no maintained
external implementation was worth a FetchContent pin. Building the
module unconditionally keeps `App` free of `#if`-guarded members and
PCM configuration differences between presets.

**Where.** `engine/src/ui/`, `engine/assets/shaders/src/{vs,fs}_imgui.sc`,
`AppConfig::enableDevUi` in `engine/src/app/app.cppm`.

---

## 2026-09-05 — Replay = the validated event/action sequence, not model output

**Decision.** AI-driven sessions are made replayable by logging the
*observed and validated* sequence — observations delivered, tool calls
proposed, accepted/rejected verdicts, resulting simulation events — and
by a replay mode that feeds previously accepted actions back into the
simulation without invoking the model. Observation/prompt/response
captures are kept for analysis but are not the authoritative replay
signal. Full determinism of physics or the engine is not a requirement
of this design.

**Why.** Requiring an LLM to reproduce identical output from identical
state is neither achievable nor desirable; nondeterminism is part of the
robot being its own thinker. What we need for debugging is to re-run
what *happened*. Logging at the validation boundary keeps the log small,
engine-owned, and independent of provider.

**Where.** Direction only; documented in
[`docs/architecture.md`](architecture.md) ("Replay and reproducibility").
Implementation is roadmap M3.

---

## 2026-09-05 — Agent memory is a first-class concept with explicit layers

**Decision.** The robot's memory is modelled as distinct layers: world
truth (simulator), perception/observations (what the robot saw), working
memory (the bounded context for one inference call), episodic memory
(remembered events), semantic memory/beliefs (which may be incomplete,
stale, wrong, or hearsay), goals, and retrieval. World truth and robot
belief are explicitly different things. Beliefs are expected to carry
provenance (subject, predicate, value, source, learned_at, confidence)
so "why does the robot believe this?" is answerable. The concrete data
model and any embedding/vector-store choice are left open.

**Why.** Reducing memory to chat history makes it invisible to gameplay
and impossible to reason about. Separate layers make memory both AI
infrastructure and a design surface: upgrades such as bigger episodic
memory or better retrieval become meaningful. Deferring the storage
technology avoids committing to embeddings before a slice shows they
are needed.

**Where.** Direction only; documented in
[`docs/architecture.md`](architecture.md) ("Memory is a first-class
concept"). Implementation is roadmap M3.

---

## 2026-09-05 — AI architecture: semantic-first, high-level tools, local-first, event-driven

**Decision.** The agent pipeline is `World → Observation → Agent →
Proposed ToolCall → Validation → Command/Intent → Simulation → Events`,
with those four nouns kept conceptually separate from the first
implementation. Perception is semantic-first: an engine-produced
structured observation is the canonical channel; rendered frames are
optional augmentation. Actions are high-level validated tools
(`moveTo`, `pickUp`, `inspect`, `say`); the simulation owns pathfinding,
locomotion, animation, and physics. Inference is local-first and
asynchronous relative to the simulation; the game loop never blocks on
the model. The agent has no fixed think tick: the simulation emits
event-driven opportunities (player interaction, world event, tool
failure, goal completion, memory trigger, `ReflectionOpportunity`), and
internal activity is expressed through explicit mechanisms (update goal,
store memory, revise belief, inspect memory) rather than persistent
free-text chain-of-thought. Which facts a robot may observe is an open
experimental question; the first observation carries only what the
first tools need.

Supersedes the "LLM integration" section of the 2026-05-17-era
architecture (provider list naming remote vendors, battery/token-budget
model, robot-customisation mechanics): those remain possible gameplay
ideas, not accepted architecture.

**Why.** The model is a decision maker, not a controller; frame-level
control would be slow, fragile, and unfun to debug. Structured
perception is inspectable and testable, and keeps the world state
authoritative in the simulation. Local-first keeps the project usable
without an external service and forces the asynchronous boundary early.
Event-driven reflection avoids burning inference on idle ticks and
gives gameplay a lever (more opportunities = smarter robot).

**Where.** Direction only; documented in
[`docs/architecture.md`](architecture.md) ("Accepted direction: AI and
agents"). Implementation is roadmap M2/M3.

---

## 2026-09-05 — Modules by default, headers where they are simply better

**Decision.** C++23 modules remain the default unit for our own modern
C++ code. Plain headers and classic TUs are used when third-party
integration, toolchain support, or concrete ergonomics make them the
better choice — not only under `src/<subsystem>/shims/`. We do not bend
the architecture or the build system just so that everything is a
module. `import std;` stays off until the supported toolchains ship it.

Supersedes the "modules-first" framing of the 2026-05-17 entry
"C++23 modules-first; no internal headers; no `import std;`". The
one-module-per-logical-unit and partition rules from that entry still
apply to module code.

**Why.** We have paid the modules integration cost and there is no
concrete reason to remove them. But treating modules as ideology has a
cost of its own: every C-API bridge or awkward header becomes a special
case to justify. The policy now names the trade-off instead of hiding
it.

**Where.** [`docs/conventions/code-style.md`](conventions/code-style.md)
(Language rules, Module layout), [`docs/build-system.md`](build-system.md).

---

## 2026-09-05 — Roboslop is the umbrella project; application-driven engine development

**Decision.** Roboslop is the project. It is an experimental but serious
platform for real-time rendering and games/simulation, AI/LLM-integrated
interactive systems, tools around the engine, graphical experiments, and
future adjacent libraries. Gorden is one application within it: a future
single-player top-down 3D game, the gameplay/AI experiment surface, and
the debug/demo app where new engine features are tried first. Gorden is
allowed to look like a technical sandbox for long stretches. At least
two more first-class apps are planned — a Shader Lab and a Level Editor
— and each gets a directory under `apps/` when work starts.

The working principle is **application-driven engine development**: a
concrete program needs a capability → implement the smallest good
solution for that program → when the concept recurs across consumers,
identify the shared abstraction → move it into the engine once the
boundary is real. No large generic subsystems are built for
hypothetical future needs.

Supersedes the 2026-05-17-era framing (in the original brief and the
first README/architecture) of "Gorden, a game on the Roboslop engine".

**Why.** The engine already had several consumers in mind (game, shader
experiments, editor), and framing everything as "the engine for Gorden"
either under-served those or invited speculative generality. Naming the
platform and stating the principle keeps the generalisation honest:
engine features earn their place by being needed twice.

**Where.** [`README.md`](../README.md), [`docs/architecture.md`](architecture.md),
[`docs/roadmap.md`](roadmap.md). Layout change is the separate
"Repository is Roboslop; `engine/` + `apps/` layout" entry below.

---

## 2026-09-05 — Exceptions are enabled; `Result` stays the API convention

**Decision.** Drop `-fno-exceptions` (and MSVC `/EHs-c-`) from the
project-wide language defaults, drop `JSON_NOEXCEPTION`, and drop
`CATCH_CONFIG_DISABLE_EXCEPTIONS` from the test target. Exception
support is left at the compiler default. The API convention is
unchanged: engine and public APIs report expected failures (asset not
found, shader compile failed, window init failed, invalid config) via
`Result<T>` / `std::expected`; exceptions are not used as control flow
in our code. `roboslop_apply_language_defaults()` remains mandatory on
every target that imports roboslop modules so PCM configuration stays
consistent — the function now only asserts `cxx_std_23`.

Supersedes the 2026-05-17 entry "`-fno-exceptions` is project-wide,
tests included".

**Why.** `-fno-exceptions` had become project identity rather than a
tool. The value we wanted — explicit, typed error paths — comes from the
`Result` convention, not from the compiler flag. The flag was costing
real integration effort: a special Catch2 configuration, a JSON
no-exception define for a library we do not link yet, comments in the
audio and window modules justifying design around it, and the standing
risk that a third-party header trips on it. Third-party code should not
need special treatment because we globally disabled a language feature.
The PCM-consistency argument in the old entry is still true, but it
argues for a *uniform* configuration, not for a specific one.

**Where.** [`cmake/Modules.cmake`](../cmake/Modules.cmake),
[`engine/tests/CMakeLists.txt`](../engine/tests/CMakeLists.txt),
convention in [`docs/conventions/code-style.md`](conventions/code-style.md),
build note in [`docs/build-system.md`](build-system.md).

---

## 2026-09-05 — Repository is Roboslop; `engine/` + `apps/` layout

**Decision.** The repository, CMake project, and Conan recipe are named
`roboslop`. The engine library moves from `roboslop/` to `engine/`; the
game moves from `gorden/` to `apps/gorden/`. The CMake target and the
C++ module namespace stay `roboslop`. The `gorden` executable now lands
at `build/<preset>/apps/gorden/gorden`; compiled shaders still land at
`build/<preset>/assets/shaders/` so the runtime asset path is unchanged.
New apps get a directory under `apps/` when implementation starts, not
as empty placeholders. Commit scopes: `engine`, `<app-name>` (e.g.
`gorden`), `apps` for cross-app layout changes.

**Why.** Roboslop is now the umbrella project and Gorden one application
within it. A repo named `roboslop` with a `roboslop/` subdirectory would read as
`roboslop/roboslop/...`; `engine/` says what the directory is. Putting
apps under `apps/` makes the multi-app structure visible without
committing to any particular future app.

**Where.** [`CMakeLists.txt`](../CMakeLists.txt), [`Makefile`](../Makefile),
[`conanfile.py`](../conanfile.py), [`scripts/`](../scripts/),
[`.clang-tidy`](../.clang-tidy), [`.clang-format`](../.clang-format),
[`engine/`](../engine/), [`apps/gorden/`](../apps/gorden/). The GitHub
remote rename is a manual step outside the tree.

---

## 2026-05-18 — Animation MVP: data layer + clip sampling, GPU skinning deferred

**Decision.** `roboslop.animation.skeleton` (Skeleton + BoneTransform
+ toMatrix), `roboslop.animation.clip` (BoneTrack + AnimationClip +
pure `sampleClip(clip, t, out)`), `roboslop.animation.state`
(AnimationState component + `tickAnimations` system +
`registerAnimationSystems` helper).

`sampleClip` is heap-free — caller provides the output span. Per-
channel keyframe streams (positions / rotations / scales) are
independent so a clip can hold position-only tracks without
allocating quaternion buffers. Lookups use `std::upper_bound` for
O(log n) keyframe search. Interpolation is lerp for positions/scales
and slerp for rotations.

`tickAnimations` advances every `AnimationState`'s clock by `dt` and
loops by `fmod`-style subtraction.

**Out of scope for this slice (deferred to a follow-up):** Assimp
rig extraction, GPU skinning (vs_skinned + bone-palette uniform),
SkinnedMesh component wiring, the lyft-demo rigged character. The
data layer + tick is enough to confirm the API shape, drive unit
tests, and let an Assimp-driven scene write to the same structures.

**Why.** Skeletal animation is the largest milestone of the chain; a
properly thin slice lands the pure-CPU pieces in one self-contained
PR-set so they can be exercised by tests and reviewed independently.
GPU skinning brings a new vertex layout, a new shader pair, frame-
arena bone-matrix allocation, and a frontend extension — none of
which would compile in isolation without the data types. Splitting
them lets each half land green.

**Where.** [`roboslop/src/animation/skeleton.cppm`](../roboslop/src/animation/skeleton.cppm),
[`roboslop/src/animation/clip.cppm`](../roboslop/src/animation/clip.cppm),
[`roboslop/src/animation/state.cppm`](../roboslop/src/animation/state.cppm),
[`roboslop/tests/animation_test.cpp`](../roboslop/tests/animation_test.cpp).

---

## 2026-05-18 — Audio: miniaudio AudioDevice + 3D listener/source systems

**Decision.** `roboslop.audio.device` exposes a move-only `AudioDevice`
that owns one `ma_engine`. `roboslop.audio` exports `AudioListener`
(tag), `AudioSource` (POD with `ma_sound*` + gain),
`updateAudioListener` / `updateAudioSources` free functions, plus
`registerAudioSystems(SystemGraph&)` that wires them into the
fixed-update graph. `installAudioDevice(world, device)` parks the
device pointer in entt's ctx storage so the systems reach it without
SystemCtx coupling — same pattern as `installJoltWorld`.

The miniaudio implementation TU is a `.c` file
(`roboslop/src/audio/miniaudio_impl.c`), the single place that
`#define`s `MINIAUDIO_IMPLEMENTATION`. Compiling it as C dodges
`-fno-exceptions` and old-style-cast warnings that miniaudio's
internals would otherwise trip on a C++ compile. The project's
`LANGUAGES` were widened to `CXX C` for this one file.

`third_party/CMakeLists.txt` marks miniaudio's include as a `SYSTEM`
interface so the engine's strict warning set
(`-Wold-style-cast`, `-Wsign-conversion`, ...) does not flag the
header in modules that just declare against it.

App owns one `AudioDevice` after `JoltWorld` in declaration order.
Gorden's demo adds an `AudioListener` tag to the camera entity and
calls `registerAudioSystems(fixed)` — no sound source yet, but the
listener pose tracks the free-fly camera and the engine path is
hot.

**Why.** miniaudio's C API stays in one TU so the C/C++ ABI boundary
is one file thick. The same `ctx`-storage pattern as physics avoids
forcing every subsystem onto `SystemCtx`; the scheduler stays free
of audio-typed dependencies. Strict warnings stay on for engine code
but lifted for a single third-party impl file — exactly where they
add no value.

**Where.** [`roboslop/src/audio/audio_device.cppm`](../roboslop/src/audio/audio_device.cppm),
[`roboslop/src/audio/audio.cppm`](../roboslop/src/audio/audio.cppm),
[`roboslop/src/audio/miniaudio_impl.c`](../roboslop/src/audio/miniaudio_impl.c),
[`roboslop/src/app/app.cppm`](../roboslop/src/app/app.cppm) (owns
AudioDevice; calls installAudioDevice),
[`third_party/CMakeLists.txt`](../third_party/CMakeLists.txt)
(SYSTEM include), [`CMakeLists.txt`](../CMakeLists.txt) (LANGUAGES
CXX C), [`gorden/src/app/main.cpp`](../gorden/src/app/main.cpp)
(AudioListener on camera, `registerAudioSystems`).

---

## 2026-05-18 — Forward directional lighting with one Lambert pass

**Decision.** `roboslop.render.lighting` exposes `DirectionalLight`
(POD: direction, color, intensity), the pure `packDirectionalLightUniform`
helper (returns the two vec4s the shader binds — normalised dir +
intensity, color), `uploadDirectionalLight(World&, dirUniform,
colorUniform)` which finds the first light in the world and pushes
its uniforms, and `LightUniforms` (a bundle of uniform handles).

`AssetCache` grows a `uniform(name, type)` accessor that caches
non-sampler bgfx uniforms with the same destruction schedule as the
sampler cache.

The textured fragment shader (`fs_textured.sc`) now computes
`max(dot(N, -L), 0) * intensity * color * albedo` plus a 0.15
constant ambient. Vertex-coloured entities (the M1 cubes/spheres)
remain on `fs_basic.sc` and are unaffected.

The gorden demo seeds one `DirectionalLight` entity and stashes a
`LightUniforms` in `world.registry().ctx()`; the main render pass
reads the uniforms from ctx and uploads them every record.

**Why.** A single directional light is the minimum to show a textured
object has shape. Routing light uniforms through ctx-storage rather
than capturing them in the pass-record lambda keeps the lambda's
captured state stable across App moves and matches the pattern
already used for `JoltWorld`. Two separate `vec4` uniforms (not one
mat or array) keep the bgfx call sites trivial; once we have
multiple lights, an array uniform replaces them.

**Where.** [`roboslop/src/render/lighting.cppm`](../roboslop/src/render/lighting.cppm),
[`roboslop/src/render/asset_cache.cppm`](../roboslop/src/render/asset_cache.cppm)
(uniform() accessor),
[`gorden/assets/shaders/src/fs_textured.sc`](../gorden/assets/shaders/src/fs_textured.sc)
(Lambert pass), [`gorden/src/app/main.cpp`](../gorden/src/app/main.cpp)
(light entity + ctx wiring). Tests:
[`roboslop/tests/lighting_test.cpp`](../roboslop/tests/lighting_test.cpp).

---

## 2026-05-18 — Asset pipeline: Assimp meshes, stb_image textures, Material component

**Decision.** Three new asset/render modules plus an `AssetCache`
extension:

- `roboslop.assets.mesh` exposes `MeshVertex` (pos+normal+uv POD),
  `MeshAsset`, and `loadMeshFile(path)` via Assimp.
- `roboslop.assets.texture` exposes an RAII `Texture` wrapper and
  `loadTexture2D(path)` via stb_image. The single
  `STB_IMAGE_IMPLEMENTATION` TU is `roboslop/src/assets/stb_image_impl.cpp`.
- `roboslop.render.material` exposes the POD `Material` component
  (program + albedo + sampler-uniform handles).

`AssetCache` grows two caches: `texture(path) -> bgfx::TextureHandle`
and `sampler(name) -> bgfx::UniformHandle`. Both ride App's
destruction order (cache dies before bgfx::shutdown). The cache's
move-ops moved from defaulted to hand-written because samplers need
explicit `bgfx::destroy` calls, and we want the moved-from cache to
be empty (not still owning handles).

The render frontend's `collectMeshDraws` now checks for a `Material`
component via `try_get`: when present, it overrides `Mesh.program`
with `Material.program` and propagates the albedo+sampler handles
into the `DrawItem`. `submitDraws` issues `bgfx::setTexture(0,
sampler, albedo)` only when both handles are valid, so vertex-coloured
draws (no Material) keep working unchanged.

New vertex layout `vertexLayoutPosNormalUv()` matches `MeshVertex`,
and the new shader pair `vs_textured.sc` / `fs_textured.sc` reads
albedo from `s_albedo`. `varying.def.sc` was extended with `v_normal`,
`v_texcoord0`, `a_normal`, `a_texcoord0`.

`submitMeshes(World&)` (the M0-era direct-submission helper) is
removed; the frontend/backend path now owns every draw.

**Why.** The plan called for "thin slice per milestone". The MVP
exercises the *engine* path end-to-end — Texture/Mesh loaders compile
and link against Assimp / stb_image — without committing checked-in
binary assets to the repo. The gorden demo builds a procedural
4×4 checkerboard texture and a 24-vertex pos+normal+uv cube inline,
running them through the same Material → DrawItem → submitDraws
pipeline that an Assimp `.obj` + PNG load will use unchanged. Real
file-loading is exercised the moment we commit a mesh; nothing in
the engine needs to change.

Routing sampler uniforms through `AssetCache::sampler(name)` (rather
than letting game code call `bgfx::createUniform` directly) keeps
their lifetime co-located with the textures they bind; otherwise
bgfx complains at shutdown about a leaked uniform. Keeping the
non-textured pos+color path alive means the M1 vertex-coloured
physics demo still works while the M2 textured cube falls alongside.

**Where.** [`roboslop/src/assets/mesh_loader.cppm`](../roboslop/src/assets/mesh_loader.cppm),
[`roboslop/src/assets/texture_loader.cppm`](../roboslop/src/assets/texture_loader.cppm),
[`roboslop/src/assets/stb_image_impl.cpp`](../roboslop/src/assets/stb_image_impl.cpp),
[`roboslop/src/render/material.cppm`](../roboslop/src/render/material.cppm),
[`roboslop/src/render/asset_cache.cppm`](../roboslop/src/render/asset_cache.cppm)
(texture + sampler caches),
[`roboslop/src/render/frontend.cppm`](../roboslop/src/render/frontend.cppm)
(Material-aware draw collection),
[`roboslop/src/render/mesh.cppm`](../roboslop/src/render/mesh.cppm)
(adds `vertexLayoutPosNormalUv()`; drops `submitMeshes`).
Shaders: [`gorden/assets/shaders/src/vs_textured.sc`](../gorden/assets/shaders/src/vs_textured.sc),
[`fs_textured.sc`](../gorden/assets/shaders/src/fs_textured.sc),
extended [`varying.def.sc`](../gorden/assets/shaders/src/varying.def.sc).
Build: [`conanfile.py`](../conanfile.py) already required `assimp/5.4.2`
and `stb/cci.20230920`; [`roboslop/CMakeLists.txt`](../roboslop/CMakeLists.txt)
links `assimp::assimp` + `stb::stb`.

---

## 2026-05-18 — Jolt physics integrated as a fixed-update subsystem

**Decision.** `roboslop.physics` exposes `JoltWorld` (move-only value
type, single-instance per process), the engine's three physics layer
filters (single-instance concrete implementations of Jolt's virtual
interfaces, hidden in `namespace detail`), and the three physics
systems registered into the fixed-update SystemGraph via
`registerPhysicsSystems(SystemGraph&)`:

- `physicsSpawn` — turns `BodyDesc` components into Jolt bodies, swaps
  in `RigidBody` and seeds `PrevTransform`.
- `physicsStep` — calls `system->Update(dt, 1, &tempAlloc, &jobSystem)`
  with `JPH::JobSystemSingleThreaded`.
- `syncPhysicsToTransform` — copies pose back to ECS `Transform`,
  capturing the prior pose into `PrevTransform`.

App owns one `JoltWorld` and calls `installJoltWorld(world, joltWorld)`
once at the start of `run()`, placing the pointer in entt's
ctx-storage. Physics systems reach the JoltWorld via
`SystemCtx.world->registry().ctx().get<JoltWorld*>()` rather than a
typed member on `SystemCtx`, so `roboslop.sched` keeps zero
dependencies on subsystem modules.

`roboslop.physics.components` is the POD-only component module:
`BodyShape` (`std::variant<SphereShape, BoxShape>`), `BodyMotion`,
`BodyDesc`, `RigidBody` (wraps `JPH::BodyID`), `PrevTransform`.

**Why.** The plan called for an MVP-thin Jolt slice (rigid bodies,
sphere + box, dynamic + static, no character controller). Putting the
JoltWorld pointer in `entt::registry::ctx()` rather than `SystemCtx`
avoids forcing `roboslop.sched` to declare physics types — a non-
exported forward declaration in a different C++23 module raised a
duplicate-class error from clang. The ctx-storage approach is the
opposite trade-off (untyped lookup at runtime) but keeps the
dependency graph clean: every subsystem that later wants engine-
owned state can register through the same ctx mechanism.

`JobSystemSingleThreaded` rather than `JobSystemThreadPool` keeps
Taskflow as the only thread-pool in the process — Jolt steps are
serialised in the SystemGraph via the `"physicsState"` write
declaration, so the cost of single-threading the solver shows up only
if the solver itself is the bottleneck. Re-evaluate when we feel it.

`-fno-exceptions` is preserved: Jolt's public surface returns error
codes or invalid handles rather than throwing, and `JPH::Ref<Shape>`
ref-counts shape lifetimes without unwind-table assumptions.

**Where.** [`roboslop/src/physics/jolt_world.cppm`](../roboslop/src/physics/jolt_world.cppm),
[`roboslop/src/physics/components.cppm`](../roboslop/src/physics/components.cppm),
[`roboslop/src/app/app.cppm`](../roboslop/src/app/app.cppm) (declares
JoltWorld member, calls `installJoltWorld`),
[`gorden/src/app/main.cpp`](../gorden/src/app/main.cpp) (M1 demo:
3 dynamic boxes + 2 dynamic spheres + 1 static ground).
Tests: [`roboslop/tests/physics_test.cpp`](../roboslop/tests/physics_test.cpp).
Build: [`conanfile.py`](../conanfile.py) already required
`joltphysics/5.2.0`; [`roboslop/CMakeLists.txt`](../roboslop/CMakeLists.txt)
links `Jolt::Jolt`.

---

## 2026-05-18 — System scheduling via Taskflow + add-order-forward DAGs

**Decision.** Two new module clusters land before any engine
fundamentals (Jolt, Assimp, lighting, audio, animation):

- `roboslop.sched` exposes `SystemDesc { name; reads; writes; run }`,
  `SystemGraph` (which materialises an add-order-forward DAG into a
  `tf::Taskflow`), and `Scheduler` (which wraps one `tf::Executor`,
  held behind a `unique_ptr` so the Scheduler itself stays movable).
- `roboslop.render.graph` exposes `PassDesc { name; reads; writes;
  record }` and `RenderGraph::execute` — sequential record on the bgfx
  API thread, dense view-ID assignment in add-order.
- `roboslop.render.frontend` exposes `FrameArena` (single
  ctor-allocation, bump-pointer, reset per frame), the flat `DrawItem`
  POD, `makeSortKey` (viewClass / viewId / program / depth packed into
  one `u64`), `collectMeshDraws`, `sortDraws`, `submitDraws`.

`AppConfig` drops the per-frame `onFixedUpdate` and `onRender`
callbacks. Game code instead supplies `onBuildGraphs(SystemGraph&,
RenderGraph&, FrameArena&)` — called once before the loop. The engine
compiles the SystemGraph after the callback returns and reuses the
same `tf::Taskflow` every frame; per-frame execution does no Taskflow
allocation. `App::run()` resets the arena at frame start, runs the
fixed graph N times per frame at the configured rate, then drives the
render graph once between bgfx begin/endFrame.

Resource ids are opaque strings (`"transforms"`, `"physicsState"`,
`"drawItems"`, `"framebuffer"`). The scheduler does not introspect
them. Edges are derived between every (earlier, later) add-order pair
that shares any of write/write, write/read, or read/write — meaning
the DAG is by construction acyclic, no cycle check needed.

**Why.** The user's mandate for the engine-fundamentals MVP chain was
**system-of-systems with parallel-friendly scheduling from day 1** and
**a render-graph + frontend/backend split with zero allocations in the
render loop**. Both needed to be in place before the five milestones
(physics, assets, lighting, audio, animation) land, so individual
subsystems can register their systems and passes without re-architecting
later. Taskflow on Conan-Center gives us a header-only work-stealing
executor; routing all systems through it means the migration to a
fully-parallel engine is a matter of declaring more resource ids
correctly, not rewriting plumbing.

Add-order-forward edges (as opposed to all-pairs) keep the graph a DAG
by construction and match how a hand-written game-loop reads top-to-
bottom — registering systems in dependency order matches dependency
order in execution. `-fno-exceptions` is preserved: we validate input
ourselves and never trigger Taskflow's throwing paths.

The frontend/backend split lives in `frontend.cppm` as free functions
over POD `DrawItem`s in a `FrameArena`. bgfx submission stays on the
bgfx API thread (`RenderGraph::execute` is sequential); CPU work
inside a pass can fan out via `tf::Subflow` later when a hotspot
warrants it. View-IDs are assigned in add-order and capped at 256 (a
bgfx limit, asserted by `RenderGraph::add`).

**Where.** [`roboslop/src/sched/scheduler.cppm`](../roboslop/src/sched/scheduler.cppm),
[`roboslop/src/render/graph.cppm`](../roboslop/src/render/graph.cppm),
[`roboslop/src/render/frontend.cppm`](../roboslop/src/render/frontend.cppm),
[`roboslop/src/app/app.cppm`](../roboslop/src/app/app.cppm),
[`gorden/src/app/main.cpp`](../gorden/src/app/main.cpp).
Build: [`conanfile.py`](../conanfile.py) requires `taskflow/3.7.0`;
[`roboslop/CMakeLists.txt`](../roboslop/CMakeLists.txt) links
`Taskflow::Taskflow`. Tests:
[`roboslop/tests/scheduler_test.cpp`](../roboslop/tests/scheduler_test.cpp),
[`roboslop/tests/render_graph_test.cpp`](../roboslop/tests/render_graph_test.cpp),
[`roboslop/tests/frame_arena_test.cpp`](../roboslop/tests/frame_arena_test.cpp).
Supersedes the per-frame-callback rows of the 2026-05-17
callback-config entry below.

---

## 2026-05-18 — Private members drop the trailing-underscore suffix

**Decision.** Private members follow the same `camelCase` rule as every
other field — no `_` suffix, no `m_` prefix. Where an accessor method
would share its name with the renamed member, the accessor is renamed
to a descriptive form: `Window::handle() → glfwHandle()`,
`Program::handle() → bgfxHandle()`,
`RenderContext::width()/height() → framebufferWidth()/framebufferHeight()`.
Accessors with no callers (`Window::cursorMode()`, `App::input()`,
`App::world()`, `App::assets()`, `App::config()`) were deleted outright
rather than renamed.

**Why.** The trailing underscore was an unwritten C++ habit, never
documented in the convention table or enforced by tidy. It contradicted
the Go-inspired naming row that says "Variables, parameters, fields |
`camelCase`". Renaming brings members into line, and renaming/removing
the colliding accessors keeps the public API expressive (`glfwHandle`
says more than `handle`) instead of leaning on a suffix as a tiebreaker.
The ctor-parameter shadowing case in `App::App(Window window, ...)`
where `input(window)` would read the moved-from parameter is resolved by
writing `input(this->window)` — members are initialised in declaration
order, so `this->window` is the just-constructed member at that point.

**Where.** Convention: [`docs/conventions/code-style.md`](conventions/code-style.md).
Enforcement: [`.clang-tidy`](../.clang-tidy) gains
`PrivateMemberSuffix: ""`. Affected modules: `app.cppm`, `input.cppm`,
`window.cppm`, `context.cppm`, `asset_cache.cppm`, `shader.cppm`,
`clock.cppm`, `world.cppm` plus the one call-site in
`gorden/src/app/main.cpp`.

---

## 2026-05-18 — Free-fly debug camera is an ECS component + pure tick

**Decision.** `roboslop::FreeFlyCamera` (module
`roboslop.render.free_fly_camera`) is an ECS component holding accumulated
yaw/pitch (radians, Euler), `moveSpeed`, `boostMultiplier`, and
`lookSensitivity`. The system `updateFreeFlyCameras(World&, Input&, dt)`
packs the current `Input` state into a `FreeFlyTickInput` value and
dispatches to the pure helper `tickFreeFlyCamera(ctrl, transform, in, dt)`
for each entity that has both a `FreeFlyCamera` and a `Transform`.
Right-mouse-button held = active (capture cursor + read motion); released
= idle (the controller skips the tick). Pitch is clamped just inside ±90°
so the forward vector never aligns with world-up. Diagonal motion is
normalised so WASD + Space don't produce a √3× speed boost.

**Why.** Matches the existing `Camera`/`ActiveCamera` pattern — controllers
are components, not free-standing classes. Storing yaw/pitch on the
controller (rather than reading them back from `Transform.rotation`) keeps
the pitch clamp authoritative and avoids the lossy quat→Euler round-trip
near the poles. Exposing a pure `tickFreeFlyCamera` over a hand-built
`FreeFlyTickInput` lets the math be unit-tested without `Input`/`World`/
GLFW. The system's RMB capture/release uses the new
`mouseButtonReleased` edge predicate so the toggle is exact (no
re-asserting state every frame, which would reset the mouse-delta).

**Where.** [`roboslop/src/render/free_fly_camera.cppm`](../roboslop/src/render/free_fly_camera.cppm),
[`roboslop/tests/free_fly_camera_test.cpp`](../roboslop/tests/free_fly_camera_test.cpp).

---

## 2026-05-18 — Input is a polled per-frame module with a pure-helper split

**Decision.** `roboslop::Input` (module `roboslop.platform.input`) wraps GLFW
key/mouse polling. Construction captures the raw `GLFWwindow*` from
`Window::handle()` and never reseats — the underlying GLFW object has a
stable address, so Input survives any Window/App move without rebinding.
`Input::beginFrame()` is called once per render frame after
`pollWindowEvents()`; it diffs the previous frame's snapshot against a
freshly polled one and caches a single mouse delta. Key/mouse-button edge
detection (`keyPressed` / `keyReleased` / `mouseButtonPressed`) and
`computeMouseDelta` are also exported as free functions on `InputSnapshot`
values so the edge logic can be unit-tested without GLFW. Cursor capture is
toggled via `Input::setCursorCaptured(bool)`, which calls
`glfwSetInputMode` directly (not through `Window::setCursorMode`) and
flags the next frame's delta to zero so the OS-driven cursor jump is not
read as motion.

**Why.** Window already owns RAII for a GLFW handle; a separate `Input`
keeps polling state, edge tracking, and snapshot history out of Window
and gives a single seam to attach gamepad/rebinding later. Storing the
raw GLFW handle (rather than a `Window&` or `Window*`) keeps Input
movable without lifetime hazards — relevant because `App::make()` returns
`Result<App>` and the contained App is move-constructed once into the
`std::expected`. Splitting edge predicates and delta computation out as
free functions on the snapshot value type means tests don't need to
initialise GLFW. Driving cursor capture from `Input` directly avoids the
Window-pointer-into-App problem entirely.

The mouse delta is captured *once per render frame* even though the
free-fly camera reads it from `onFixedUpdate` (which may run multiple
sub-steps per frame). Mouse motion is an angular quantity (pixels per
frame), not a velocity, so re-applying the same delta across sub-steps
is the correct semantics — re-polling per sub-step would multiply look
sensitivity by the sub-step count.

**Where.** [`roboslop/src/platform/input.cppm`](../roboslop/src/platform/input.cppm),
[`roboslop/src/platform/window.cppm`](../roboslop/src/platform/window.cppm)
(adds `CursorMode` enum, `setCursorMode`, `handle()` accessor for Input),
[`roboslop/tests/input_test.cpp`](../roboslop/tests/input_test.cpp).

---

## 2026-05-17 — Camera is an ECS component picked by `ActiveCamera` tag

**Decision.** `roboslop::Camera` (module `roboslop.render.camera`) is an
ECS component whose `projection` field is a `std::variant<Perspective,
Orthographic>`. A separate empty-struct `ActiveCamera` tag marks which
camera-entity the renderer reads each frame. `findActiveCamera(world)` is
the public selection helper; `applyActiveCamera(world, viewId, w, h)`
reads the active camera, computes view+proj via glm
(`perspectiveRH_{ZO,NO}` / `orthoRH_{ZO,NO}` chosen by
`bgfx::getCaps()->homogeneousDepth`), and pushes them to bgfx with
`setViewTransform`. Per-draw model matrices come from `Transform` via
`bgfx::setTransform`, and the shader reads bgfx's built-in
`u_modelViewProj` — no custom MVP uniform.

**Why.** A camera-as-component plays naturally with future requirements:
robot-POV rendering for LLM-vision snapshots, split-screen, debug
free-cameras. The tag-based active selection avoids App-side state.
Using bgfx's idiomatic per-view + per-draw transform machinery (rather
than a hand-rolled `u_mvp` uniform) makes multiple views and instancing
cheaper later. Right-handed +Y-up -Z-forward matches glm's defaults so we
never reach for `bx::mtx*`.

**Where.** [`roboslop/src/render/camera.cppm`](../roboslop/src/render/camera.cppm),
[`roboslop/src/scene/transform.cppm`](../roboslop/src/scene/transform.cppm),
[`roboslop/src/render/mesh.cppm`](../roboslop/src/render/mesh.cppm)
(per-draw `setTransform`),
[`gorden/assets/shaders/src/vs_basic.sc`](../gorden/assets/shaders/src/vs_basic.sc)
(uses `u_modelViewProj`).

---

## 2026-05-17 — `AssetCache` owns `Program`s; `onSetup` receives a ref

**Decision.** `App` owns a `roboslop::AssetCache` (module
`roboslop.render.asset_cache`) that caches `Program`s keyed by
`(vsName, fsName)`. The cache hands out non-owning `ProgramHandle`s
storable on components. `AppConfig::onSetup`'s signature changes to
`Result<void>(World&, AssetCache&)` so game code requests programs
without holding handles itself. v1 is render-thread-only and has no
invalidation; both are documented in the module header.

**Why.** The first iteration of the game forced `main.cpp` to hold a
`Program` outside `App` so the bgfx handle survived until
`bgfx::shutdown()`. That wart blocks any second mesh entity (it would
need its own outside-`App` lifetime) and pushes lifecycle concerns into
game code where the engine should own them. Threading the cache into
`onSetup` is the minimal API extension — `onFixedUpdate` and `onRender`
don't need the cache (handles live in components by then), so their
signatures stay untouched.

**Where.** [`roboslop/src/render/asset_cache.cppm`](../roboslop/src/render/asset_cache.cppm),
[`roboslop/src/app/app.cppm`](../roboslop/src/app/app.cppm).
Supersedes the `onSetup` row of the 2026-05-17 callback-config entry
below.

---

## 2026-05-17 — App drives game code via callbacks on `AppConfig`

**Decision.** `AppConfig` carries three `std::function` hooks — `onSetup`,
`onFixedUpdate`, `onRender` — plus an `assetRoot` path. `App` owns the
loop, the `Window`, the `RenderContext`, and a `World`; the game's hooks
participate at well-defined moments. No virtual `GameApp` interface, no
engine-driven default render pipeline.

**Why.** A callback config is the lowest-ceremony API that still lets the
engine fix loop ordering and lifecycle. Virtual interfaces force a class
hierarchy the game doesn't need at this stage. Engine-driven default
pipelines hide control flow — keeping `onRender` empty by default and
letting the game opt into `submitMeshes(world)` keeps draw-call ordering
explicit and debuggable.

**Where.** [`roboslop/src/app/app.cppm`](../roboslop/src/app/app.cppm),
hook documentation in [`docs/architecture.md`](architecture.md).

---

## 2026-05-17 — ECS facade: `World` exposes `forEach`, not entt views

**Decision.** `roboslop::World` (module `roboslop.ecs`) wraps
`entt::registry` and exposes `create`, `destroy`, `valid`, `emplace`,
`get`, `tryGet`, `has`, `remove`, and a templated `forEach<Components...>`
that takes a callback. It does **not** export `entt::basic_view`. Engine
subsystems that need iterator-level access (snapshot serialisation,
custom traversals) reach the underlying registry through `World::registry()`.

**Why.** Re-exporting `entt::basic_view` through a C++23 module loses the
non-member `operator==/!=` of entt's sparse-set iterator on the consumer
side — range-`for` does not compile in importing TUs without also
including entt headers, which defeats the purpose of the wrapper. A
custom view wrapper would lose entt's iteration ergonomics and
performance. `forEach` keeps the API self-contained without paying that
cost, since entt's `view::each` accepts callbacks and dispatches on
their signature.

**Where.** [`roboslop/src/ecs/world.cppm`](../roboslop/src/ecs/world.cppm).

---

## 2026-05-17 — `roboslop::App` + semi-fixed timestep from day 1

**Decision.** The engine ships an `App` class (module `roboslop.app`) as
the only entry point a game uses; `App::make()` returns `Result<App>` and
`App::run()` drives the loop. The loop is **semi-fixed** (Gaffer-style
accumulator) with a default 60 Hz fixed update and a variable render
slot. `FixedTimestep` clamps frames longer than 0.25 s.

**Why.** Physics (Jolt) requires a stable fixed step; switching the tick
model after subsystems already plug into it forces an API change across
every consumer. Introducing it now — while the fixed-update body is still
empty — is essentially free. `App` similarly anchors lifecycle so games
don't drive `init/tick/shutdown` themselves.

**Where.** [`roboslop/src/app/app.cppm`](../roboslop/src/app/app.cppm),
[`roboslop/src/time/clock.cppm`](../roboslop/src/time/clock.cppm). Frame
loop overview in [`docs/architecture.md`](architecture.md).

---

## 2026-05-17 — Push policy: agents never push

**Decision.** No automation pushes to the remote. The user pushes manually
when they decide to publish work.

**Why.** Keeps publication a conscious step rather than a side effect of
landing a commit.

**Where.** Workflow constraint; not enforced in code.

---

## 2026-05-17 — bgfx, miniaudio via CMake FetchContent

**Decision.** Pull bgfx (+ bx, bimg, shaderc) and miniaudio at configure
time via CMake `FetchContent` with pinned SHAs/tags. Sources land under
`build/<preset>/_deps/` (gitignored), never in the working tree.
`imgui_impl_bgfx` is deferred until the dev-UI subsystem needs it.

**Why.** A custom Conan recipe was the original plan (~3–5 days of work);
submodules were rejected for repo overhead and recursive-init cost.
FetchContent keeps the working tree free of upstream sources. The recipe
path can be re-introduced later by mechanical conversion.

**Where.** [`third_party/CMakeLists.txt`](../third_party/CMakeLists.txt).
SHA pins live as cache variables at the top of that file.

---

## 2026-05-17 — `-fno-exceptions` is project-wide, tests included

**Decision.** Engine, game, and test binaries all compile with
`-fno-exceptions`. Catch2 uses `CATCH_CONFIG_DISABLE_EXCEPTIONS` so its
macros expand to a TRY/CATCH abstraction that compiles cleanly.

**Why.** C++23 module PCMs are sensitive to compiler config — a mismatch
on `-fno-exceptions` between producer and consumer rejects the PCM with a
"configuration mismatch" error. Keeping code-gen uniform avoids the trap.

**Where.** [`cmake/Modules.cmake`](../cmake/Modules.cmake)
`roboslop_apply_language_defaults()`. Test target reuses the same helper.

---

## 2026-05-17 — Error handling: per-subsystem enums + lightweight wrapper

**Decision.** Each engine subsystem defines its own `enum class FooError`
plus a `toError(FooError) -> roboslop::Error` mapping. The wrapper
`roboslop::Error { category, code, message, context }` is what crosses API
boundaries. `Result<T> = std::expected<T, Error>`.

**Why.** A single global enum forces `core` to know about every subsystem
and doesn't scale. `std::expected<T, std::string>` wastes an allocation on
every error and loses the ability to branch on category.

**Where.** [`roboslop/src/core/error.cppm`](../roboslop/src/core/error.cppm).
Subsystem-specific enums and mappings land with each subsystem.

---

## 2026-05-17 — C++23 modules-first; no internal headers; no `import std;`

**Decision.** Engine and game code lives in `.cppm` module interface units.
One module per logical unit; partitions only for multi-implementation
cases (e.g. LLM backends). No `include/` tree for internal code; headers
are tolerated only under `src/<subsystem>/shims/` for non-modularised
C-API bridges. `import std;` stays off — use classical `#include <print>`,
`#include <expected>` etc. inside `.cppm` files.

**Why.** Modules give better build times and cleaner interfaces.
`import std;` lacks mature shipping support in libc++/libstdc++ for the
supported compilers; revisit once that lands.

**Where.** [`cmake/Modules.cmake`](../cmake/Modules.cmake) defines
`roboslop_add_module_library()`. `roboslop/src/core/{version,error}.cppm`
serve as the reference shape.

---

## 2026-05-17 — Minimum compilers: Clang 19+ / GCC 15+

**Decision.** Promised support is Clang 19+ and GCC 15+. MSVC is tracking
but unvalidated.

**Why.** Realistic floor for C++23 modules through CMake
`FILE_SET cxx_modules`. Older versions have known module-scanner bugs and
partial standard-library support for `std::print`, `std::expected`,
`<ranges>`.

**Where.** [`README.md`](../README.md) prerequisites.

---

## 2026-05-17 — Build system: CMake + Conan + Ninja, Makefile entry point

**Decision.** CMake 3.30+ with Ninja as the only supported generator,
Conan 2.x for Conan-Center dependencies with one profile per
(OS, compiler), CMakePresets for tool integration, Makefile as the
human-facing entry point. Five presets: `debug`, `release`,
`relwithdebinfo`, `asan-ubsan` (per-target sanitiser flags), `tsan`
(dedicated Conan profile that rebuilds dependencies with
`-fsanitize=thread`).

**Why.** Ninja is the only generator with reliable C++23-module scanning.
Conan + CMakePresets gives reproducible toolchain wiring without manual
flag chains. The Makefile keeps the common loop a single keystroke long.

**Where.** [`CMakeLists.txt`](../CMakeLists.txt),
[`CMakePresets.json`](../CMakePresets.json),
[`conanfile.py`](../conanfile.py),
[`conan/profiles/`](../conan/profiles/),
[`Makefile`](../Makefile),
[`scripts/`](../scripts/).

---

## 2026-05-17 — Coding style: LLVM-base format, pragmatic tidy, Go-inspired naming

**Decision.** `.clang-format` based on LLVM with 4-space indent, Attach
braces, column 100, include-blocks regrouped (system → stdlib → roboslop
→ gorden → other). `.clang-tidy` enables the
bugprone / modernize / performance / readability / cppcoreguidelines /
misc / portability / concurrency families, with ~14 disables for game-dev
pragmatics (magic-numbers, pointer-arith, reinterpret-cast, POD member
visibility, etc.). Naming rules per
[`docs/conventions/code-style.md`](conventions/code-style.md).

**Why.** Modern C++ baseline, but practical for game and graphics code
where bit-twiddling and POD structs are routine; the disabled checks can
be re-enabled selectively in hot paths.

**Where.** [`.clang-format`](../.clang-format),
[`.clang-tidy`](../.clang-tidy).
