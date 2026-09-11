# Roadmap

Direction, not a contract. Milestones are ordered by what we want to
learn next, and each one is meant to be a *vertical slice* that pulls a
real requirement through the engine — see the application-driven
principle in [`architecture.md`](architecture.md). Dates are
deliberately absent. Decisions taken while executing a milestone are
logged in [`decisions.md`](decisions.md); this file only says where we
are heading.

Legend for status words used below: **current state** is what the tree
does today, **accepted direction** is what we have agreed to build,
**likely future direction** describes later possibilities, and **open
question** is something we are deliberately leaving to experiment.

**Next major priority: M5 — make Gorden playable.** M0–M4 retain their
historical status below. The remaining M3 reflection/replay work stays on
the roadmap, but is no longer necessarily next; broad editor expansion
also waits behind the first playable room.

---

## M0 — Roboslop platform reset

**Goal.** Reposition the repository from "Gorden, a game on the Roboslop
engine" to "Roboslop, a platform with Gorden as one application".

- Rename the project and establish the `engine/` + `apps/` layout.
- Rewrite README and architecture to describe the current truth.
- Document application-driven engine development as the working
  principle.
- Document the AI direction: semantic-first perception, high-level
  actions, local-first inference, reflection opportunities, memory as a
  first-class concept, replay of validated actions.
- Make the C++ modules and exceptions policies pragmatic.

**Definition of done.** A new developer can read README + architecture +
roadmap and understand what Roboslop is, what Gorden is, why several
apps exist, how engine features are expected to emerge, and which AI
architecture we are aiming at.

**Status.** Done with this milestone's commits.

---

## M1 — Shader Lab: live shader development

**Goal.** The first new Roboslop application: a Shadertoy-like sandbox
that is *not* limited to a fullscreen quad. It is the next big
engine-driving experiment.

Vertical slice:

1. Start the app and pick at least one simple geometry (a sphere or
   plane is enough to begin with).
2. Apply a shader program to it and move the camera around.
3. Edit the shader source on disk; the app detects the change.
4. Recompile through `shaderc`, then replace the running program
   hygienically on the render thread.
5. On a compile failure, show the diagnostics and keep the last working
   program alive.

Engine needs this is expected to surface: runtime asset identity,
shader recompilation, safe hot replacement of GPU resources,
diagnostics surfacing, geometry/camera inspection, and a dev UI where
it is actually needed (this is where the ImGui slot in `third_party/`
finally gets filled).

Later Shader Lab targets, not part of the first slice: imported meshes,
terrain, fullscreen passes, multiple geometry types side by side.

**Explicitly avoided.** Designing a complete generic asset system first.
Build what Shader Lab needs; generalise when Gorden or the editor needs
the same thing.

**Status.** First vertical slice done (2026-09-05): `apps/shaderlab`
renders a sphere and a plane, watches the shader sources, recompiles
through the `shaderc` binary on a worker thread, swaps the program on
the render thread, and shows diagnostics in an ImGui panel while the
last working program stays live. The "later targets" above remain open.

**Resolved questions** (details in [`decisions.md`](decisions.md)).

- Runtime compilation shells out to the `shaderc` binary; linking it in
  stays an option if diagnostics or latency become a problem.
- Shader sources live under each app's `assets/shaders/`; the engine's
  own ImGui pair lives under `engine/assets/shaders/`. No shared
  top-level `assets/` yet.
- File watching polls `last_write_time` at 250 ms, app-side.

**Open questions.**

- Asset identity: programs are still raw bgfx handles copied into
  components and rewritten on swap. Revisit when the editor (M4) or a
  second hot-reloaded asset type needs a stable id.

---

## Cross-cutting: developer tooling

Not a milestone, but the milestones lean on it. Exists today: the dev
UI with a window registry (View menu, F1, persisted layout and
visibility), per-app settings under the XDG config dir, an in-memory
virtual filesystem with live and host mounts (`roboslop.vfs`), a small
shell over it (`roboslop.shell`), and a terminal window
(`roboslop.ui.terminal`). Gorden exposes its agent log, observation,
transcript, status, and settings as files. Candidates: a monospace
font and mounting Shader Lab's diagnostics. A diegetic gameplay role for
the terminal is now accepted for M5, separately from this developer UI.

## M2 — Gorden agent vertical slice

**Goal.** The minimal real AI/gameplay loop, proving the agent/runtime
boundary rather than building a game.

```text
player + robot
    ↓
semantic observation
    ↓
local LLM backend
    ↓
a few high-level tools (moveTo, inspect, say)
    ↓
validation
    ↓
simulation
```

What lands: an `Observation` produced by the engine from the robot's
point of view, a provider abstraction with one *local* backend, a tool
schema for a handful of actions, a validation layer that turns a
proposed `ToolCall` into a `Command`, and the simulation-side execution
of those commands (pathfinding/locomotion can be trivial to start with).

Inference must be asynchronous relative to the fixed-update loop from
the first version; the game loop never blocks on the model.

**Status.** First slice done (2026-09-05): `roboslop.llm` with a
scripted backend and an OpenAI-compatible HTTPS backend; in Gorden the
`gorden_agent` library (observation, tools + validation, kinematic
locomotion, `AgentBrain`) and a chat panel. Verified with the scripted
provider in tests and against OpenAI (`gpt-4.1-mini`).

**Resolved questions** (details in [`decisions.md`](decisions.md)).

- Runtime: one OpenAI-compatible backend, verified against OpenAI first;
  a llama.cpp server is the same backend with another base URL. The
  local-first direction is unchanged, the order of verification is not.
- First `Observation`: robot and player positions, named entities within
  a radius with distance, recent events, the player's message. Nothing
  else.
- Validation lives in the app (`gorden.agent.tools`), as planned.

**Open questions.**

- When a second agent-driven app appears, which of observation building,
  validation rules, and the brain's event loop move into the engine.
- Line-of-sight and other perception limits: today every named entity
  in range is visible.

---

## M3 — Agent memory, reflection, replay

**Goal.** First real version of the memory model and the replay
machinery described in [`architecture.md`](architecture.md).

- Episodic memory, beliefs with provenance, goals.
- Retrieval that picks which old memories become relevant again.
- Reflection opportunities as event-driven prompts for internal work
  (update goal, store memory, revise belief, inspect memory).
- Event/action logging of the *validated* sequence.
- Replay mode that feeds previously accepted actions back into the
  simulation without asking the model again.

Keep the implementation small enough to observe and debug by hand.

**Status.** Memory slice done (2026-09-07): `gorden.agent.memory` holds
episodes, beliefs with provenance and goals; five validated tools
(`remember`, `recall`, `believe`, `setGoal`, `closeGoal`) are the only
way to write them; active goals and beliefs ride along in every
observation while episodes come back through `recall`. Memory survives
a session through a new engine save format (`roboslop.scene.savegame`,
see [the save format](save-format.md)) with explicit Save/Load in the
Escape menu and `save` / `load` in the terminal. Details in
[agent memory](agent-memory.md). Reflection opportunities beyond the
existing event triggers, and replay, are not done. They remain planned,
but the next major priority is M5 playability rather than completing all
of M3 first.

**Resolved questions** (details in [`decisions.md`](decisions.md)).

- Storage is plain structs in vectors with keyword retrieval; no
  embeddings, no embedded store.
- The memory model lives in Gorden, not the engine. The engine got only
  the save container, which is app-agnostic.
- Persistence is a general save game under `stateDir()`, not a memory
  file, and it is written only when asked for.

**Open questions.**

- Reflection opportunities: today the memory tools are used during a
  normal think; there is no separate "voluntary internal activity"
  trigger yet.
- What is the unit of replay — one log per session, per agent, or per
  world?
- Retrieval quality: word matching is enough to observe, but we have not
  yet seen it fail in a way that tells us what to build next.

---

## M4 — Level editor vertical slice

**Status.** First primitive-scene slice implemented ahead of M3 (2026-09-06):
`apps/editor` creates, selects and transforms objects, edits solid materials
and a directional light, saves/loads versioned JSON, provides undo/redo, and
previews physics with restoration on Stop. Gorden consumes the same scene
document. See [scene editing](scene-editor.md) for controls and limits.

**Goal.** The first concrete editor use case. Not "build Unity".

Enough to: open or create a small scene, select an entity, manipulate
its transform, save, load, and start/preview the scene.

Engine needs this is expected to surface: scene/world serialisation,
asset identity (shared with Shader Lab), selection, transform gizmos,
undo/redo, editing versus runtime state, physics preview, possibly live
preview of a running scene.

**Open questions.**

- The editor is a separate executable; JSON version 1 is the first scene format.
- Imported models have a format, a loader and a place in the scene format
  (`geometry: "model"` + path, see [models](models.md), 2026-09-07).
  Hierarchy, prefabs and richer asset identity remain open; the model path
  is the only identity today.
- How much of the editor's UI is shared with Shader Lab's dev UI?

---

## M5 — First playable room

**Status.** Player movement slice implemented (2026-09-08): separate visible
placeholder avatar with Jolt capsule collision, camera-relative WASD/gamepad
movement, third-person orbit with obstruction handling, camera audio and
player save/load reset. See [player controls](player-controls.md). The
static human `player.glb` is now attached to the moving player with its
authored colours and scale (see [models](models.md)). The first two-wheel
Gorden model is also attached to the runtime robot and turns with its
kinematic movement. Room authoring has begun: the default scene is now an
enclosed room with the reusable terminal-station props, a power unit, a
central ceiling light and a blocked primitive exit. The visible fixture uses
a colocated finite-range point light, with the directional light retained as
broad fill. The fullscreen computer interaction, anywhere chat and Escape pause menu are
implemented (2026-09-10); developer panels require `--dev`. The safety
interlock puzzle is implemented (2026-09-11): the terminal verifies the tag
and relay order that Gorden reads up close in conduit bay C, and opening the
door removes its collision and slides it into the wall. The progress survives
save/load; see the [first-room design](gorden-first-room-design.md). A deterministic Routine
mode, so that the loop is completable without an LLM, remains.

**Goal.** Start in a small locked room with Gorden, explore and interact,
solve a computer/terminal puzzle that unlocks/opens the exit, and leave.
The complete loop must be playable without an LLM:

```text
move/explore → observe → interact → reason / use terminal
    → change world state → progress
```

Vertical slice:

- A real player entity, separate from the camera, with movement state
  and character movement/collision.
- Distinct visible player and Gorden models through the existing `.glb`
  pipeline. Rigid/static models are sufficient; animation and GPU skinning
  are not prerequisites.
- A simple third-person follow/orbit camera targeting the player, with
  camera-relative horizontal movement and an audio listener following
  the camera. Free-fly remains a developer/editor camera.
- Keyboard/mouse and Xbox-style controller support: WASD/left stick move,
  mouse/right stick look, E/A interact, Escape/B cancel. Prefer normalized
  GLFW gamepad input through the existing snapshot seam.
- Basic Gorden-side interactions: objects expose semantic state and
  possible interactions; requests pass simulation-side validation. Future
  AI interaction must use the same gameplay rules as human interaction.
- Interact with the computer to enter Terminal mode, suspend locomotion,
  and route input to the terminal; cancel returns to Explore mode. An
  ImGui/full-screen overlay is enough. Keep the developer terminal separate.
- A small terminal/system puzzle that changes actual door state and the
  physical/visual world, allowing the player to leave. The generator or
  another subsystem may participate; the solution is not chosen yet.
- Relevant semantic gameplay state and world events visible to Gorden
  through perception rules, so it can perceive/react to progression.
- Save/load of progression, restoring consistent gameplay and physical state
  using the existing save foundation with Gorden-owned state as needed.

**Definition of done.** The player can complete the room with keyboard/mouse
or an Xbox-style controller, without an LLM or developer-panel intervention.
The exit initially blocks progress, terminal success changes the simulation
and opens a route out, and saved progression survives loading. Gorden's
observation can describe the relevant gameplay state, not just object names
and coordinates.

**Open implementation questions.** Richer interaction targeting beyond the initial
2 m monitor proximity check and concrete state/API vocabulary; the puzzle
and its commands/files; controller terminal navigation/text entry; authored
gameplay setup and save representation. See the accepted boundaries in
[architecture](architecture.md#accepted-direction-first-playable-gorden-room).
Do not front-load general input rebinding, a camera framework, an engine
interaction hierarchy, broader editor work or a multi-agent runtime.

---

## M6 — Agent/world interaction

**Status.** Longer-term direction; not implemented.

Build on M5's gameplay rules with richer affordances and AI use/interact
proposals. Distinguish game/story directives from agent-created goals and
current intention; directives are not casually editable by the LLM.
Relevant world changes, action completion/failure and becoming idle with
an active goal may create autonomous deliberation opportunities. Thinking
stays event/opportunity-driven and asynchronous; simulation controls
locomotion and physics. Planning algorithms and concrete directive/intention
representations remain open. M3 reflection/replay work remains available
as gameplay and debugging needs justify it.

---

## M7 — Connected facility

**Status.** Likely future direction; not implemented or planned in detail.

Extend beyond one room into multiple spaces. Facility network/camera access
and other integrations may serve as capability unlocks or story progression,
exposing information, terminal functions, interactions or AI tools. Examples
include `camera_access`, `facility_network`, `robot_radio` and
`security_override`; neither their mechanics nor a generic plugin system
are specified. Agent shell access stays inside game abstractions/VFS.

---

## M8 — Additional NPC agent

**Status.** Likely future direction; not required for the locked room.

Introduce the first genuinely additional AI-controlled NPC, with friendly,
neutral or hostile directives. That concrete need should drive any
`AgentBrain`/runtime refactoring for multiple agents, each with its own
event queue, model activity, memory, directives, capabilities and action
state. The current one robot/player pair is acceptable until then; a
conceptual runtime/instance split is a possibility, not a committed API.

## Other later work

Candidates remain GPU skinning and rigged characters when animated characters
need them, multiple lights, robot character collision/pathfinding, richer
editor tooling and further "fusion projects" that reuse the engine core.
These are not prerequisites for making the first room playable. The existing
OpenAI-compatible backend already supports remote endpoints; additional
providers should follow a concrete need.
