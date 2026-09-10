# Scene editing

The first editor builds small rooms from cubes, spheres and planes. It shares
the scene reader, primitive geometry, materials and physics instantiation with
Gorden. Scene files describe authored state; live GPU and physics handles are
never saved.

## Start

```sh
make editor
make editor ARGS="/absolute/path/to/scene.json"
make gorden ARGS="--scene /absolute/path/to/scene.json"
```

Both apps default to `assets/scenes/room.json` relative to the build directory.
CMake stages it from the authoritative
`apps/gorden/assets/scenes/room.json` whenever configuration runs. Editing the
staged build copy is temporary: a later configure restores the checked-in
source. To author the default room, open and save the source path explicitly;
use another path outside `build/` for experiments you want to retain.

## Edit a room

1. Use **New** for an empty scene or enter a path in **File** and press **Open**.
2. Add a cube, sphere or plane, or one of the models listed from
   `assets/models/**/*.glb` (see [models](models.md)). Select an object in the list
   or click its visible geometry; models are picked by their bounds. A yellow
   wire box around the bounds and colored axis handles identify the selection.
   Imported model positions place the model's authored origin, not its bounds
   centre; follow [model origins and placement](models.md#model-origins-and-placement)
   when grounding it or putting it on another prop.
3. Edit position, rotation (degrees) and scale in the inspector. For direct
   manipulation, choose Move, Rotate or Scale and drag a colored endpoint.
   Move and Rotate use world axes; Scale uses local axes. Rotation changes by
   dragging along the projected axis. An axis viewed end-on may be unavailable;
   move the camera or use the numeric fields.
4. Choose a material. **Shared color** changes every object using that material;
   **Make material unique** creates a separate editable material for the object.
   Models show their file path instead: their materials come from the file.
5. Choose `static` physics for floors/walls and `dynamic` for falling objects.
   The collider follows primitive dimensions and scale; a model gets a box
   around its bounds. A sphere collider requires uniform scale; planes are
   visual only, so use a thin cube for floors.
6. Duplicate/Delete and Undo/Redo operate on authored objects. A continuous
   property or handle drag is one undo step. History holds the last 128 edits.
7. **Use current camera as scene start** records the current camera pose. Merely
   flying around does not dirty the document. RMB + WASD flies, Space/Ctrl moves
   vertically, and Shift increases speed.
8. Enter a path and **Save**. Changing the File path then saving is Save As.
   Replacing a different existing file asks for confirmation. New/Open/Close
   offer Save/Discard/Cancel when edits are unsaved. Escape does not quit the editor.

**Play** instantiates the document with physics and disables editing. **Stop**
destroys those physics bodies and recreates the authored scene. The simulation
never overwrites the document or undo history. Gorden reads the same environment,
adds its robot and player, and exposes object names to the robot's observation.

## Format and boundaries

Version 1 JSON contains `objects`, `materials`, `camera`, one directional
`light`, and optionally one finite-range `pointLight`. Each object has a stable
string `id`, `name`, `geometry` (`cube`, `sphere`,
`plane`, `model`), material ID, transform and `body` (`none`, `static`, `dynamic`).
A `model` object also carries a `model` path relative to the asset root
(`models/crate.glb`); the key is absent on primitives. Model objects draw with
the materials inside the file and ignore the document material.
Transforms store position/scale as XYZ arrays and rotation as a WXYZ unit
quaternion. Geometry names identify shared unit primitives (sphere diameter 1,
cube side 1, plane side 1). Material IDs refer to RGB colors in the document.
The point light stores world-space `position`, RGB `color`, `intensity` and
`range`; the editor can add, remove and tune it independently of visible
geometry. Version 1 scenes without `pointLight` remain valid.

The reader rejects unsupported versions, duplicate IDs, missing material
references, absolute or escaping (`..`) model paths, nonfinite transforms,
invalid rotations, and unsupported colliders.
Positive scale is limited to 0.01..1000. Limits: 2000 objects, 1000 physics bodies,
256 materials. Saves validate first, close the temporary output, then rename it
over the destination. A failed load keeps the editor's current document intact.

This slice uses a flat object list; document materials are solid Lambert
colours, and textures come only from model files. `SceneRuntime` uploads each
referenced model once (one static mesh per part, one texture per material)
and gives the object a single entity with a `ModelInstance`, so a model moves,
falls and is picked as one unit; its collider is a box around the model's
bounds scaled by the object. Hierarchy, prefabs, terrain brushes, textures on
document materials, collision visualization and snapping are future work.
Gorden's robot still moves directly toward targets; walls do not add pathfinding.
The known native-Wayland surface-loss issue remains separate from scene editing.

## Verification

`make test` includes primitive winding, scene validation and file round trips,
editor history/picking, and repeated physics body creation/removal.

The display-dependent integration executable is not part of headless CTest:

```sh
cd build/debug
apps/editor/editor_runtime_smoke /tmp/your-existing-test-directory
```

It loads the room, adds an object, undoes/redoes, saves and reopens the document,
then renders ten Play/Stop cycles and verifies falling poses and restored
authored transforms. It writes `roundtrip.json` inside the supplied directory.
