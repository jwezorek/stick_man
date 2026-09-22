# stick_man Animation System

**Current implementation and design constraints**  
**Reviewed against the September 22, 2026 source selection**

## 1. Status

Animation is now a working subsystem rather than a future design.

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

The important work before adding many more action types is therefore not “build animation.” It is to harden the boundaries around the existing animation model: pose/rig reconciliation, character-scoped validation, animation remapping for character copy/paste, a more explicit action-extension contract, and stronger diagnostics/tests.

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

### 3.3 Known rig-evolution gap

This exact compatibility rule exposes an important unfinished area.

Structural topology edits already reconcile animation **actions** by deleting actions whose persistent references would dangle, but they do not reconcile stored **poses**. `initialize_animation_assets()` creates Default only when the pose list is empty; it does not add newly adopted/created rig nodes to existing poses or remove deleted nodes from them.

Consequences include:

- adding a node/skeleton to a character can make existing poses incomplete;
- deleting a node/skeleton can leave stale node entries in poses;
- either condition makes `pose_compatible()` fail;
- an animation using such a pose can no longer be opened until the pose is repaired/recreated.

This should be resolved before broadening the action vocabulary. See section 18.

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

That property should remain non-negotiable for future action types.

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

### Current UI mismatch

The timeline currently disables the easing control for translation actions and presents them as linear even though Core stores and evaluates an easing value for every action.

Before adding additional action families, decide one of two policies and make the type model/UI agree:

1. **Easing is universally supported** — expose the existing Core behavior for translations; or
2. **Easing is an action capability** — explicitly model which action types support which timing controls instead of relying on UI special cases.

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

This helper is used by topology-edit cascade logic and should remain the authoritative dependency surface for future action types.

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

## 13. Character-scoped validation: current limitation

`animation_assets::validate(topology, character_root_bone)` currently proves that each persistent referenced ID resolves somewhere in the supplied project topology and that the animation ordering constraints are valid.

It does **not** prove that every target/reference belongs to the character that owns the animation.

For example, malformed data could theoretically contain an action on character A that names a valid node or bone owned by character B. The project replacement code even preserves atomic undo for this malformed cross-character case when cascading deletions.

Normal editor authoring does not intentionally create such actions, but this is a weaker Core invariant than the character-owned animation model implies.

Before more action types are added, topology-aware validation should receive a character/rig scope and require that:

- Rigid Translation target skeletons are members of the owning character;
- Rigid Rotation target bones are in the owning character rig;
- IK effectors, pivots, and pins are in the owning character rig and satisfy their same-skeleton rules;
- Bone reference frames refer to bones in the owning character rig;
- the character root bone resolves inside the owning character.

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

## 17. Current action-extension problem

The four existing action types are semantically coherent, but adding a fifth type currently requires coordinated changes in several places.

At minimum a new action may need updates to:

- the `action_data` variant;
- structural validation;
- topology dependency enumeration;
- write-scope analysis;
- reference-frame ordering logic if applicable;
- absolute-time evaluation;
- serialization/deserialization;
- gesture authoring;
- timeline label/presentation;
- property controls and visibility logic;
- canvas adornments/editing;
- character-copy remapping once that is implemented.

Some sites use exhaustive `std::visit`, but others use chains of `std::get_if` with a fallback such as `unsupported_actions`. That means adding a variant alternative does not guarantee a compile-time failure at every missing integration point.

Before the action set grows, establish an explicit action-extension contract. This does not necessarily require a heavyweight class hierarchy or runtime plugin system. A small compile-time traits/visitor layer would be enough if it makes the required semantics obvious and exhaustive.

A useful action contract should cover:

```text
type identity / serialization name
structural validation
persistent dependency enumeration
character-scope validation
write scope
evaluation
ID remapping
presentation name/color/capabilities
whether it has a canvas adornment
whether it supports easing/path editing/etc.
```

---

## 18. Work to complete before adding many new action types

The following items are higher priority than expanding `action_data`.

### 18.1 Reconcile poses with rig evolution

This is the most immediate semantic hole.

Define a single Core policy for topology/membership changes and poses. At minimum:

- remove entries for nodes that cease to belong to the character;
- define how newly added nodes enter Default;
- define how newly added nodes affect named poses;
- provide an explicit named-pose recapture/update operation in the editor;
- keep animations from becoming unusable merely because an ordinary supported topology edit occurred.

A reasonable policy is for Default to follow character membership automatically while named poses remain explicit authored snapshots that can become “needs update” when new nodes appear. Whatever policy is chosen should be centralized rather than encoded opportunistically in the UI.

### 18.2 Add animation-asset ID remapping and preserve animation on character copy/paste

Whole-character copy/paste currently copies the rig and artwork but not the character's poses/animations.

`mdl::project::paste_character()` creates fresh skeleton/node/bone IDs and already remaps artwork bone bindings and the character root bone. Animation assets are initialized empty, so copied animations are lost.

Before action payloads become more numerous, add a Core animation remapping facility that can remap every topology reference in:

- pose node-position keys;
- Rigid Rotation bone IDs;
- IK Rotation effector/pivot IDs;
- Rigid Translation skeleton/reference-bone IDs;
- IK Translation effector/pin/reference-bone IDs;
- future action payloads through the same extension contract.

Then make character clipboard copy/paste include animation assets as well as artwork.

### 18.3 Enforce character-scoped action integrity

Strengthen topology-aware validation as described in section 13 so “resolves in project” becomes “resolves in this character and satisfies this action's structural rules.”

### 18.4 Make action integration exhaustive

Introduce a single documented/compile-time action extension surface so a new type cannot be half implemented.

### 18.5 Improve validation diagnostics

The current public validation path mostly throws broad `std::invalid_argument` messages, while `animation_evaluation` reports only lists of invalid/unsupported action IDs. Some editor adornment refresh code catches all exceptions and silently suppresses them.

A structured issue type would make failures much easier to surface and test:

```text
action ID
issue code
referenced object ID when relevant
human-readable diagnostic
```

This should be usable by deserialization validation, editor commit validation, and Animation Mode preview.

### 18.6 Add focused Core regression tests

The supplied source selection does not include the repository's test/build files, so this document does not make a claim about the full repository's current test coverage. Regardless, the semantics now justify a dedicated animation regression suite covering at least:

- seeking directly versus playback-step equivalence;
- evaluation at action start/mid/end/after-end;
- layer ordering;
- reference-frame capture before the selected action;
- Animation Root versus Character Root versus Bone behavior;
- Rigid and IK translation path/adornment agreement;
- IK action-local pins;
- reference dependency placement;
- topology-edit action cascades and undo;
- pose/rig reconciliation once implemented;
- character-copy ID remapping once implemented;
- cross-character reference rejection;
- JSON round trips for every action/path/reference type.

### 18.7 Resolve action capability policy

Easing is the visible current example: Core supports it for translation while the UI suppresses it. Future actions may similarly differ in whether they have an angle, path, target frame, pins, or direct adornment.

Model those differences deliberately rather than accumulating `holds_alternative` UI special cases.

---

## 19. Lower-priority animation/editor cleanup

These should not block the semantic work above, but they are worth addressing during stabilization:

- `rig_interaction.cpp` currently ignores returned FABRIK result values in two ordinary manipulation paths; decide whether failures should produce UI feedback or simply terminate the gesture cleanly.
- `constraint_tool.cpp` intentionally refuses to edit bone constraints in Animation Mode and contains a TODO for action-local constraint authoring. That should remain a future action/design decision rather than mutating persistent rig constraints during animation editing.
- `animation_skeleton_pane.cpp` still contains a TODO for resynchronizing its tree selection after rebuild.
- the evaluator's `unsupported_actions` fallback is not a useful forward-compatibility mechanism while deserialization rejects unknown action types; either keep it as an internal assertion/diagnostic or make the extension contract exhaustive.

---

## 20. Rules future actions should preserve

Any new action type should preserve these system-level invariants:

1. **Absolute-time determinism.** Evaluation at `t` starts from the animation's base pose, not the previous displayed frame.
2. **Persistent-reference integrity.** Ordinary editor topology edits must not leave dangling action references.
3. **Character locality.** An action may affect/reference only objects in its owning character unless a future feature explicitly defines cross-character semantics.
4. **No silent retargeting.** Persistent IDs are authoritative.
5. **Explicit ordering.** If an action samples mutable intermediate state, the ordering dependency must be represented/validated rather than inferred from playback history.
6. **Self-contained action data.** Playback must not depend on transient Selection-tool state.
7. **Core/editor separation.** Core owns semantic data/evaluation; Qt owns authoring interaction/presentation.
8. **Serializable authored state only.** Derived caches and transient preview state are rebuilt after load.
9. **Copy/remap support.** Every persistent topology reference introduced by an action must participate in the common remapping path.
10. **Exhaustive integration.** A new action should not compile as “supported” while silently lacking evaluation, validation, persistence, or editor presentation.

With those foundations in place, adding new action kinds becomes a local extension of the model rather than another round of architectural repair.
