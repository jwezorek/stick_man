# stick_man Animation System

**Current implementation reference**  
**Reviewed against the September 22, 2026 source selection**

## 1. Status

Animation is a working, persisted editor subsystem.

The current implementation includes:

- character-owned Default and named poses;
- character-owned animations with ordered layers and timed actions;
- deterministic absolute-time evaluation from an animation base pose;
- four implemented action types:
  - Rigid Rotation;
  - IK Rotation;
  - Rigid Translation;
  - IK Translation;
- persistent motion paths for translation actions;
- straight, cubic Bezier, and smooth multi-segment spline paths;
- Animation Root, Character Root, and Bone-relative translation frames;
- action-local pin storage for IK Translation;
- layer-order validation for animated reference-frame dependencies;
- a persistent Animation pane;
- Animation Mode with a detached working topology;
- a timeline with playback, scrubbing, action selection, move/resize, and action-property editing;
- canvas rotation/path adornments for selected actions;
- direct editing of translation paths on the canvas;
- artwork rendering against the evaluated Animation Mode topology;
- Core serialization and validation of animation data in the packaged project format;
- topology-edit dependency discovery and atomic deletion of actions whose persistent references would otherwise dangle.

Core also keeps pose membership synchronized across structural edits, validates animation references against the owning character's rig, and remaps pose/action references when an entire character is copied and pasted. The editor-side action-specific behavior is centralized so the four action alternatives share one consistent timeline/gesture/adornment integration layer.

---

## 2. Ownership model

Animation data belongs to `sm::character`.

Conceptually:

```text
project
    topology
    characters
        character
            rig
            character_root_bone
            artwork
            animation_assets
                Default pose
                named poses
                animations
```

The Core types are value-semantic animation data. The editor owns editing session state such as the current playhead, selected action, open Animation Mode session, timeline scroll/zoom, and provisional gesture state.

`animation_assets` contains:

```cpp
object_id default_pose;
std::vector<pose> poses;
std::vector<animation> animations;
```

Each `animation` stores:

```cpp
object_id id;
std::string name;
object_id base_pose;
std::vector<animation_layer> layers;
```

Each `animation_action` stores:

```cpp
object_id id;
animation_time start;      // integer milliseconds
animation_time duration;   // positive integer milliseconds
easing easing;
action_data data;          // std::variant of the four action payloads
```

Persistent references are object IDs, never display names.

---

## 3. Poses

A pose is a concrete map from persistent node ID to world-space node position.

```cpp
struct pose {
    object_id id;
    std::string name;
    std::unordered_map<object_id, point> node_positions;
};
```

`capture_pose()` records every node in the character rig. `apply_pose()` applies the positions for node IDs that resolve in the supplied topology.

### 3.1 Default pose

When a character is created and has no animation assets yet, Core captures the current rig as a built-in Default pose. New animations use the Default pose as their initial base pose.

The editor currently supports:

- New Pose from Current;
- Apply Pose;
- Duplicate Pose;
- Rename Pose;
- Delete Pose;
- Update Default from Current;
- Set a pose as an animation's base pose.

The Default pose cannot be deleted through the Animation pane. Deleting a named pose that is used by animations prompts to reassign those animations to Default.

### 3.2 Exact compatibility

`pose_compatible()` currently requires an exact node-set match:

- every node in the current character rig must be present in the pose; and
- the pose must not contain additional node IDs.

Animation Mode refuses to open an animation whose base pose is not exactly compatible with the current rig.

### 3.3 Rig evolution and pose reconciliation

`reconcile_animation_poses()` keeps stored poses coherent with character membership after supported topology and membership edits.

The current policy is deliberately different for Default and named poses:

- the Default pose removes nodes that are no longer members of the character;
- the Default pose adds newly introduced member nodes using their current world positions;
- existing Default entries are preserved rather than being recaptured from the current on-screen pose;
- named poses remove entries for nodes that cease to belong to the character;
- named poses are **not** automatically extended when new nodes are added to the rig.

As a result, adding geometry can make an existing named pose incomplete. `pose_compatible()` continues to require an exact node-set match, so applying an incomplete pose or opening an animation that uses it as its base pose is rejected until that named pose is updated/recreated. Removing geometry, by contrast, prunes deleted-node entries from both Default and named poses.

Pose reconciliation participates in the same undoable semantic snapshots as the structural edit, so undo/redo restores the exact pre/post-edit pose data.

---

## 4. Layers and time semantics

Layers are ordered **bottom to top**. They are composition order, not bone tracks or property tracks.

Within a layer:

- actions are ordered by start time for evaluation;
- action intervals may not overlap;
- adjacent intervals are allowed.

Across different layers, time intervals may overlap.

An animation's duration is the greatest action end time. Action start times must be non-negative and durations must be positive.

At a requested absolute time `t`, an action's normalized progress is:

```text
0                      when t <= start
(t - start) / duration when start < t < start + duration
1                      when t >= start + duration
```

Completed actions therefore continue to contribute their final transformation after their interval ends.

---

## 5. Absolute-time evaluation

The central semantic rule is:

> Animation evaluation is reconstructed from the base pose at absolute time `t`; it is never integrated from the previously displayed frame.

`evaluate_animation()`:

1. verifies that the base pose contains every node in the detached working topology;
2. reapplies the base pose to that topology;
3. walks actions in ordinary composition order;
4. computes each action's absolute progress at `t`;
5. applies that action's eased contribution to the current intermediate topology;
6. records per-action evaluation context used by editor adornments.

This gives deterministic scrubbing, seeking, pause/resume, and playback. The result at 2300 ms does not depend on whether the editor previously displayed 2284 ms, 0 ms, or 8000 ms.

That absolute-time rule is the basis for playback, seeking, and adornment evaluation throughout the current implementation.

---

## 6. Easing

Core defines:

```text
linear
ease_in
ease_out
ease_in_out
smoothstep
```

Core applies the action's easing to normalized action progress before evaluating the action's geometry.

For translation actions, this eased progress is the fraction of total motion-path arc length.

### Editor easing policy

Every `animation_action` stores an easing value and the Core evaluator applies it before evaluating the action geometry.

The editor currently exposes easing for the two rotation actions. Translation actions are authored and edited as linear: their action-editor capability reports `uses_easing = false`, authoring coerces their easing to `linear`, and the timeline disables the easing control for them.

This means Core can still evaluate a persisted/programmatically supplied non-linear easing value on a translation action, but the normal editor workflow creates linear translation actions.

---

## 7. Implemented action types

`action_data` currently contains exactly four alternatives:

```cpp
std::variant<
    rigid_rotation,
    ik_rotation,
    rigid_translation,
    ik_translation
>
```

### 7.1 Rigid Rotation

Stored data:

```text
target bone ID
pivot = root | tip
total angle
propagation = hierarchy | bone_only
```

The selected root/tip endpoint is fixed. Progress scales the authored total angle. The operation uses the existing bone rotation machinery and its constraint handling.

`hierarchy` applies the ordinary hierarchical rotation behavior. `bone_only` requests the existing bone-only propagation behavior.

The evaluator records the incoming pivot and rotating endpoint positions as the action's rotation evaluation context. The canvas uses that context to display/edit the selected rotation arc against the actual incoming pose.

### 7.2 IK Rotation

Stored data:

```text
effector node ID
pivot node ID
total angle
```

The effector and pivot must resolve to distinct nodes in the same skeleton. The pivot is treated as fixed. At progress `p`, the evaluator rotates the incoming effector vector around the pivot by `angle * p` and invokes FABRIK to solve toward the resulting target.

The pivot is therefore both the geometric rotation center and the IK boundary for write-scope analysis.

### 7.3 Rigid Translation

Stored data:

```text
target skeleton IDs
motion_path
translation reference
optional reference bone ID
```

Rigid Translation always targets complete skeletons. Every targeted skeleton receives the same world-space translation derived from the local motion path.

### 7.4 IK Translation

Stored data:

```text
effector node ID
action-local pinned node IDs
motion_path
translation reference
optional reference bone ID
```

The pin list is persistent action data. Playback never reads the editor's current Selection-tool pin state.

All pins must resolve to nodes in the same skeleton as the effector and must not include the effector itself. The evaluator solves the effector toward the path-derived target with those nodes supplied as fixed boundaries.

---

## 8. Motion paths

A translation `motion_path` is a **displacement path in the action's local reference frame**.

Its first point is always `(0, 0)`.

Implemented geometries are:

### Straight

```cpp
line_path { start, end }
```

`start` must be zero.

### Curve

```cpp
cubic_bezier_path { start, control1, control2, end }
```

`start` must be zero.

### Spline

```cpp
spline_path { std::vector<cubic_bezier_path> segments }
```

A spline must contain at least one segment. Consecutive segments must meet at the same point, and interior handles must satisfy the implemented smooth-knot check.

### 8.1 Arc-length evaluation

Motion paths are evaluated by distance rather than by raw Bezier parameter.

The arc-length table is derived/cache-only data and is not serialized. Cubic segments are sampled at 64 subdivisions to construct the table.

This makes translation progress visually closer to uniform distance along a curve and keeps serialized data limited to authored geometry.

---

## 9. Translation reference frames

Translation paths are stored locally and transformed into world space using one of three reference modes:

```cpp
enum class translation_reference {
    animation_root,
    character_root,
    bone
};
```

The character has a persistent `character_root_bone`.

A bone reference frame is:

```text
origin      = bone root/parent node world position
orientation = angle from bone root/parent to bone tip/child
```

### 9.1 Animation Root

Animation Root is fixed for the entire evaluation.

It is the frame of the character root bone in the animation **base pose**:

```text
origin      = base-pose position of root-bone parent node
orientation = base-pose root -> tip angle
```

It does not follow animation actions that subsequently move or rotate the root bone.

### 9.2 Character Root

Character Root is the frame of the character root bone in the **currently evaluated intermediate topology**.

It therefore follows preceding actions that move or rotate that frame.

### 9.3 Bone

Bone uses the frame of the explicitly stored reference bone in the **currently evaluated intermediate topology**.

It likewise follows preceding actions that move or rotate that reference bone.

### 9.4 The action must not move its own path frame

For reference-relative translations, the selected action's reference frame is captured from the intermediate pose **immediately before that action contributes**.

Conceptually:

```text
base pose
    + all preceding actions
        -> incoming topology for selected action
        -> evaluate selected action reference frame
        -> apply selected action
```

The selected action does not first move the frame and then use that moved frame for its own path.

The same evaluation context is used by the selected path adornment during playback, scrubbing, and pause.

### 9.5 Rigid versus IK translation use of the path

For a rigid translation:

```text
local displacement = motion_path(eased progress)
world displacement = reference_frame.vector_to_world(local displacement)
translate every target skeleton by world displacement
```

The frame origin is relevant to displaying the path, while the action itself applies a displacement vector.

For an IK translation:

```text
incoming effector position = effector position before this action
local displacement         = motion_path(eased progress)
world displacement         = reference_frame.vector_to_world(local displacement)
target                     = incoming effector position + world displacement
solve IK toward target
```

This makes IK Translation compose with actions below it rather than snapping back to an authoring-time world position.

---

## 10. Reference-frame ordering rules

A reference-relative action creates a real dependency on the frame it samples.

The evaluator itself uses explicit composition order. Core additionally rejects layer orders that would let a later action modify a reference frame already captured by an earlier reference-relative translation.

`animation_action_write_scope()` computes a conservative set of node IDs an action may move.

Current write-scope rules are:

- Rigid Translation: every node in every target skeleton;
- Rigid Rotation: every node in the owning skeleton except the fixed pivot endpoint;
- IK Translation: nodes reachable from the effector before crossing action-local pins;
- IK Rotation: nodes reachable from the effector before crossing the pivot node.

For ordering validation, Character Root and Bone-relative translations capture both endpoint nodes of the relevant bone. Animation Root is fixed from the base pose and therefore does not create a mutable-frame dependency.

The validation scan walks layers bottom-to-top and actions chronologically. Once a frame endpoint has been captured, a later action may not have that endpoint in its write scope.

The timeline uses `place_animation_action()` when an action is created or moved. It preserves the relative order of existing actions and moves only the edited action upward to the first valid layer/boundary when possible.

This is an authored ordering rule, not a runtime dependency graph or iterative solver.

---

## 11. Action persistent dependencies

`animation_action_dependencies()` is the central enumeration of persistent topology references stored by an action.

Current dependencies are:

| Action | Persistent topology dependencies |
| --- | --- |
| Rigid Rotation | target bone |
| IK Rotation | effector node, pivot node |
| Rigid Translation | target skeletons, plus reference bone when reference mode is Bone |
| IK Translation | effector node, all action-local pins, plus reference bone when reference mode is Bone |

Animation Root and Character Root are symbolic character-level semantics and do not store the current root bone as an action-local persistent dependency.

The same exhaustive persistent-reference semantics are also used for animation ID remapping, so dependency discovery and whole-character copy/paste cannot silently diverge.

---

## 12. Referential integrity during topology edits

Ordinary editor topology operations are not allowed to leave animation actions with dangling persistent references.

Before a destructive structural edit, Core computes `topology_edit_effects`, including the actions whose dependencies would be removed. The editor can present that effect to the user. On confirmation, those actions are deleted as part of the same logical edit.

Undo restores topology, character membership, artwork metadata, and the removed animation actions together through the membership snapshot used by the project command.

Important consequences:

- deleting a bone removes a Rigid Rotation that targets it;
- deleting an IK effector or pin removes the corresponding IK action;
- deleting a reference bone removes a Bone-relative translation;
- deleting/regenerating a target skeleton removes a Rigid Translation that names that skeleton;
- Core does **not** silently retarget actions to similar surviving objects.

Corrupt or externally modified project files are a separate case. Deserialization/validation rejects unresolved action references rather than treating malformed data as a normal editor state.

The older design idea of retaining broken actions in the timeline for later repair is **not the current behavior**.

---

## 13. Character-scoped validation

`animation_assets` has two validation layers.

`validate()` checks the animation data structurally: asset/action IDs, base-pose existence, finite values, valid enums, motion-path shape, non-overlapping actions within a layer, and the type-specific shape of each payload.

`validate(topology, rig_skeletons, character_root_bone)` then validates the assets in the context of the owning character. It requires that:

- every rig skeleton exists;
- a non-nil character root bone belongs to that rig;
- every node stored by every pose belongs to the rig;
- every active persistent node/bone/skeleton reference carried by an action resolves inside the rig;
- IK Rotation's effector and pivot belong to the same skeleton;
- IK Translation's pins belong to the effector skeleton;
- Animation Root / Character Root translations have a usable character root bone;
- the animation's reference-frame ordering rules are valid.

`mdl::project::edit_animation_data()` validates the edited copy before committing the undoable command. Structural project operations likewise validate the candidate semantic state before committing it, and project loading rejects invalid character-scoped animation data.

---

## 14. Animation Mode

Animation Mode is an editor session around a detached topology, not a second project model.

When an animation is opened:

1. the owning character and animation are resolved;
2. the selected base pose must be compatible with the current character rig;
3. the character's skeletons are copied into a detached working `sm::topology` while preserving persistent IDs;
4. editor `user_data` is cleared on the copies;
5. the base pose is applied;
6. the canvas is bound to/evaluates the working topology for animation editing;
7. the persistent project topology remains the source of authored character structure.

Structural editing is intentionally constrained while Animation Mode is active. Animation edits are undoable through the project command system, and the editor tracks the animation-edit session boundary so ordinary topology/model operations are not mixed into the detached session.

### 14.1 Artwork preview

Artwork remains character-owned project data, but rendering can resolve sprite transforms against an optional geometry topology.

During Animation Mode the canvas passes the evaluated working topology to artwork resolution, so sprites follow animated bone geometry without copying artwork into the working topology.

This is the intended separation:

```text
persistent character artwork semantics
        +
evaluated detached rig geometry
        ->
preview sprites
```

---

## 15. Timeline and action editing

The Animation Timeline owns stick_man-specific timeline editing and transport behavior.

Current behavior includes:

- absolute-time playhead seeking;
- play/pause/stop/start/end transport;
- periodic playback updates;
- action rectangles with start/duration editing;
- movement between layers;
- insertion/placement through Core ordering rules;
- selection of an action;
- common action properties for start, duration, and easing;
- type-specific controls for rotation and translation data;
- canvas adornments for geometric parameters;
- direct translation-path handle editing;
- direct rotation-angle adornment editing.

The reusable timeline control remains presentation/interaction infrastructure; action semantics belong to Animation Timeline/Core.

### 15.1 Canvas-first authoring

New actions are primarily authored by performing familiar rig manipulations with the Selection/Animate tool rather than filling out a property form from scratch.

`rig_interaction` supplies shared manipulation semantics and converts completed gestures into action payloads:

- ordinary rigid rotation -> Rigid Rotation;
- ragdoll-style rotation -> IK Rotation;
- complete-skeleton translation -> Rigid Translation;
- ragdoll translation -> IK Translation.

IK Translation captures the relevant pinned nodes into the action at authoring time.

Translation pointer samples are converted into the requested Straight/Curve/Spline persistent path rather than being replayed as raw pointer motion.

---

## 16. Persistence

The current project package format is version 6. Core also accepts project JSON versions 4 and 5 through explicit compatibility paths.

Animation JSON contains:

- Default pose ID;
- poses and node-position maps;
- animation IDs/names/base pose IDs;
- ordered layers;
- action IDs;
- integer-millisecond start/duration;
- easing enum;
- type-specific action data.

Current serialized action type strings are:

```text
rotation
ik_rotation
translation
ik_translation
```

Translation paths serialize only authored geometry. Arc-length samples are derived after load.

Translation reference strings are:

```text
animation_root
character_root
bone
```

A legacy numeric translation-reference form can still map its old Animation Root and Character Root values. The obsolete legacy node-relative mode is deliberately rejected because it cannot be losslessly converted to the current bone-relative semantics.

Unknown action/path/reference types are rejected during load rather than preserved opaquely.

---

## 17. Action integration structure

The action vocabulary is a closed `std::variant`:

```cpp
using action_data = std::variant<
    rigid_rotation,
    ik_rotation,
    rigid_translation,
    ik_translation
>;
```

Core handles the alternatives through explicit visitors for validation, evaluation, write-scope analysis, ordering, serialization, and deserialization.

Persistent project-object references are centralized in `sm_animation_action_semantics.hpp`. `for_each_action_persistent_reference()` has one constrained overload per action type and intentionally has no generic fallback. It drives both:

- `animation_action_dependencies()` for topology-edit cascade discovery; and
- `remap_animation_assets()` for whole-character ID remapping.

The editor has a similar central dispatch layer in `animation_action_editor.cpp`. Action-specific timeline presentation, authoring messages, easing capability, equivalence checks, gesture construction, tool-property synchronization, pin capture, and adornment installation are dispatched through `action_editor<Action>` specializations. There is intentionally no primary implementation, so adding an `action_data` alternative requires an explicit editor implementation at compile time.

The timeline and generic rig-interaction code therefore operate on `action_data` without duplicating most type switches throughout the UI.

`animation_evaluation` still contains an `unsupported_actions` vector for reporting, but the current evaluator exhaustively handles all four variant alternatives and does not place any current action into that list.

---

## 18. Current system invariants

The implementation is built around the following rules:

1. **Absolute-time determinism.** Evaluation at `t` starts from the animation's base pose, not the previously displayed frame.
2. **Persistent-reference integrity.** Ordinary editor topology edits do not leave dangling action references.
3. **Character locality.** Persistent references in a character's poses/actions must resolve inside that character's rig.
4. **No silent retargeting.** Persistent object IDs are authoritative; deleted dependencies cause action removal rather than heuristic rebinding.
5. **Explicit composition order.** Layers compose bottom-to-top and actions within a layer are evaluated chronologically.
6. **Reference-frame capture is pre-action.** Character Root and Bone frames are evaluated from the intermediate topology immediately before the action that uses them.
7. **Self-contained action state.** Playback depends on persisted action data, including IK Translation pins, rather than current Selection-tool state.
8. **Core/editor separation.** Core owns animation semantics, validation, evaluation, persistence, dependency analysis, and remapping; Qt owns authoring interaction and presentation.
9. **Derived data stays derived.** Motion-path arc-length caches and editor preview/adornment state are rebuilt rather than persisted.
10. **Whole-character copying is semantic.** Pose node IDs and every persistent action reference are remapped alongside the copied topology, while animation asset identity and authored values are otherwise preserved.
