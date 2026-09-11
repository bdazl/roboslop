# Gorden player movement

The player is separate from the camera and uses the static low-poly human
from `models/player.glb`; Gorden uses the two-wheel `models/gorden.glb`. The player
turns toward movement and keeps that heading when idle. There is no walk
animation yet. The computer and chat now have gameplay interfaces; the terminal
puzzle remains a later slice.

## Controls

| Action | Keyboard / mouse | Xbox-style controller |
|---|---|---|
| Move | WASD | Left stick |
| Orbit camera | Hold right mouse button and move mouse | Right stick |
| Use nearby computer | E / on-screen button | A |
| Open chat from anywhere | T / on-screen Chat button | X |
| Cancel interaction / back / pause menu | Escape | B |
| Open pause menu while exploring | Escape / Menu button | Start |
| Toggle developer tools (`--dev` only) | F1 | — |

Chat is a fixed panel with conversation history and a text field. Robot replies
also appear as queued, timed subtitles above the action buttons while exploring.
Chat suspends player controls while the world and robot continue. The computer
opens a terminal covering the entire game window; its Chat button opens a
conversation and Escape returns to the terminal. Escape then leaves the computer;
another Escape opens the pause menu. Escape in Settings returns to the menu.
Keyboard/mouse are required for text entry and menu buttons in this slice.

The pause menu offers Resume, Settings, explicit Save/Load and Quit. It pauses
all fixed simulation systems, including physics, movement and the agent pump.
An already-running provider request can finish in the background, but its result
is not applied until simulation resumes. Rendering and UI input continue; paused
time is discarded rather than caught up on resume.

The first room's `terminal-monitor` is accessible within 2 m of its live transform
(3D distance from the player's capsule centre). This is a minimal proximity
interaction, without facing or line-of-sight targeting. Loading a scene/save
re-evaluates the target; scenes without that id have no computer interaction.
The terminal currently reuses the existing sandbox shell/VFS; door commands and
puzzle-specific files are not implemented.

`make gorden` and `make run-gorden` pass `--dev`. Launching the executable without
that flag gives the game interface alone. Performance, Agent log and Developer
terminal are available only with `--dev` (and `ROBOSLOP_DEV_UI=ON`). F1 hides those
tools without hiding the game interface. Performance starts with mean FPS, frame
time and GPU time; expand Details for the graph and percentiles.

Release the right mouse button to use interface buttons. Entering an interaction
releases camera capture; a cancelled mouse hold must be released before looking
again. Keyboard focus in a developer window suspends movement and looking,
including gamepad input. An unfocused game window ignores all gameplay input.
A mouse drag starting over UI cannot become camera capture by moving off it.

Movement follows the camera's horizontal heading, irrespective of pitch.
Speed is 4 m/s, with normalized diagonal input and proportional stick
magnitude outside a radial 20% deadzone. The first GLFW-normalized gamepad
is used; connecting/disconnecting is picked up by the next frame's poll.
Mouse look measures displacement, whereas stick look measures angular
velocity. Mouse displacement accumulates across frames without a fixed
step and is consumed once, even during catch-up steps.

## Simulation and camera

`gorden.player` owns the input mapping, orbit state and player component.
Its Jolt `CharacterVirtual` capsule is 1.8 m tall and 0.7 m wide, centred
on the player transform, with gravity, wall sliding, floor support,
a 45-degree slope limit and steps up to 0.3 m. There is no jump or sprint
in this slice. Visual scale does not scale the capsule.

`gorden.player_visual` attaches a `ModelInstance` to the player at unit scale.
Its local transform offsets the authored feet by -0.9 m and turns glTF +Z
forward to gameplay -Z. The capsule, camera target and save position stay
centred. `SceneRuntime` owns the borrowed model buffers and textures; its
model cache survives scene replacement during load.

Physics spawns and steps scene bodies before player movement, then the
camera follows the resolved player position. Audio follows the camera;
robot observations and saves refer to the player entity. A 0.2 m sphere
sweep retracts the camera before an obstruction. Orbit distance is 4 m,
with bounded pitch. Free-fly remains available in the editor and Shader Lab.

The avatar starts at `(0, 0.4, 4)` in the demo scene. Custom scenes still
use that spawn; authored spawn points belong to the later room setup.
The default scene is an enclosed room; its exit is still a static blocker.
The escape puzzle is not built yet.

Save/load keeps the existing `app.player` transform. Invalid app payloads
are rejected before changing the live scene or actors. Loading discards the
controller's velocity and cached contacts, recreating it at the restored
position on the next fixed step. Camera orbit is session state and is not
saved. Older saves used the camera as the player; those positions are now
interpreted as character positions and settle under gravity.

Gorden app payload version 2 saves the authored model at unit scale. Version 1
saves remain loadable: their sphere-placeholder scale is replaced with unit
scale while position and rotation are preserved.
