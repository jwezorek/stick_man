# `sm::character`: Current Architecture

**Reviewed for Animation 2.0 Phase 2 — September 27, 2026**

## 1. Purpose

A character is the stable authored boundary that groups one or more skeleton components with the semantic data that belongs to them.

A skeleton is topology. A character is ownership and semantics.

The current character owns:

```text
identity and display name
rig membership
artwork / appearances
standalone poses
minimal Animation V2 assets
```

Loose skeletons remain valid project objects and can be promoted/adopted into a character later.

---

## 2. Core representation

The current Core shape is approximately:

```cpp
class character {
    object_id id_;
    std::string name_;
    project& owner_;
    sm::rig rig_;
    sm::artwork artwork_;
    animation_assets animation_data_;
};
```

`sm::rig` stores persistent skeleton IDs. It does not duplicate geometry. The authoritative nodes, bones, constraints, and skeletons live in `sm::project::topology()`.

```text
project topology
    owns nodes/bones/skeletons/constraints

character
    owns membership + character semantics
```

There is no persistent Animation-V1 character-root-bone designation. Animation 2.0 derives an animation's incoming frame from its eventual pose domain and topology rather than from a user-selected action reference frame.

---

## 3. Why character and skeleton are separate

A character can legitimately contain multiple disconnected skeletons: a conventional body plus detached face controls, floating pieces, or components produced by a structural edit. Therefore one skeleton cannot be used as the user's stable character identity.

A character survives changes in the number or connectivity of its member skeletons. `sm::rig` records membership, and each owned skeleton carries a non-owning parent-character relationship back to the character. `project::has_consistent_membership()` validates both directions.

A valid character:

- contains at least one skeleton;
- contains no duplicate skeleton membership;
- owns only live skeletons from the project topology; and
- agrees with each member skeleton's parent-character link.

---

## 4. Character-owned artwork

Every character owns one `sm::artwork` value containing its logical image resources, semantic slots, appearances, state mappings, and local sprite transforms.

Artwork bone references are remapped when a topology replacement deliberately preserves a semantic bone while assigning it a fresh ID. Artwork can also retain an unresolved slot when its referenced bone is removed; rendering simply skips slots that no longer resolve to a bone in the owning character.

See `Appearances.md` for the full artwork model.

---

## 5. Standalone poses

`animation_assets` still owns the built-in Default pose and user-created named poses. These are independent pose assets, not Animation V2 keyframes.

Standalone poses retain their existing behavior:

- Default Pose is initialized from the character rig;
- named poses can be captured, renamed, updated, applied, duplicated, and deleted;
- pose node IDs are reconciled when topology membership changes;
- pose data is serialized with the character; and
- whole-character clipboard operations remap pose node IDs.

Pose reconciliation follows the current policy:

- Default removes nodes that leave the character and adds newly introduced member nodes at their current positions;
- named poses remove nodes that leave the character but are not automatically extended when the rig gains new nodes.

A named pose whose node set no longer exactly matches the current rig is therefore incompatible until it is updated or recreated.

Standalone poses have no relationship to animations. There is no animation `base_pose`.

---

## 6. Animation V2 Phase 2 assets

Animation 2.0 Phase 2 extends the persistent animation asset with authored skeletal keyframes and minimal transition bookkeeping:

```cpp
struct animation {
    object_id id;
    std::string name;
    std::vector<pose_keyframe> keyframes;
    std::vector<pose_transition> transitions;
};
```

Each `pose_keyframe` has a stable ID, an optional display name, and skeletal pose state. Animation keyframes are deliberately separate from standalone named-pose assets. Standalone poses retain their existing world-node representation; animation keyframes store root translations plus local bone rotations so they represent authored skeletal state rather than commands from the removed action system.

Unnamed keyframes are displayed as `Pose N` according to their current order. That generated label is editor presentation and is not serialized. Transition records are currently limited to persistent identity and default duration, with exactly one transition between adjacent keyframes. Interpolation, transition solving, positional tracks, and playback remain unimplemented.

Phase 1 project data remains compatible: an animation serialized with only `id` and `name` loads as an empty keyframe sequence.

See `animation.md` for the Animation 2.0 semantics and Phase 2 implementation status. `old_Animation.md` remains historical documentation of the removed action-based system only.

---

## 7. Animation Mode editing session

Opening an animation still edits a detached working topology rather than the persistent character topology. Phase 2 adds derived keyframe selection plus persistent animation-data authoring to that session:

```text
animation asset identity
character identity
detached working topology
selected keyframe ID (editor/session state)
session undo stack
session redo stack
original animation-data snapshot
```

The working topology is copied from every skeleton in the character's current rig, including its persistent constraints. For the current Phase 2 implementation, the full character rig remains the poseable region; restricted pose-domain editing is still deferred. When an animation already has keyframes, entering Animation Mode selects and displays the first stored pose directly. An empty animation begins from the character's current pose until the user chooses **Add Pose**.

Keyframe selection is presentation/session state and is never serialized. Selecting a keyframe applies its exact stored skeletal pose to the detached topology without interpolation or transition evaluation. Loading a keyframe for display does not itself create an undo entry or record a pose edit.

Canvas manipulation of a selected keyframe is undoable in the session stack and captures the resulting complete skeletal state back into that keyframe. Commands target the owning character, animation, and keyframe by stable IDs, so undo/redo remains frame-aware even if selection changes after the edit. Undo/redo of a pose edit reselects the affected keyframe so the result is visible.

Keyframe creation, duplication, rename, and deletion are likewise session commands. Deleting the final remaining keyframe is valid. Undo of deletion restores the original keyframe identity, sequence position, content, and affected transition records.

Persistent keyframe data is part of `sm::project` and therefore serializes even while Animation Mode is active. The detached working topology, selection, thumbnails, previous-pose ghost, and other rendering caches remain nonpersistent editor state.

When Animation Mode ends, authored animation-data changes are committed to ordinary document history as one document edit. The pre-existing document undo/redo stacks remain isolated from the per-session stack while Animation Mode is active.

---

## 8. Session undo/redo, pins, thumbnails, and ghosting

Animation Mode continues to have its own undo and redo stacks. Normal Edit -> Undo/Redo actions route to those stacks while the session is active and return to document history when the session ends.

Node pins remain editor/session posing aids. In Animation Mode pin toggles are recorded in session history and are restored/discarded on exit. They are not persistent constraints or Animation V2 transition pins.

Persistent rotation and rigid-triangle constraints are copied into the detached topology so they continue to influence IK, but constraint creation, editing, deletion, and adornment dragging are disabled in Animation Mode.

The Pose Strip renders each keyframe from a temporary posed topology rather than by capturing the live canvas. This keeps previews independent of canvas zoom, selection, handles, and current editing state. Artwork is resolved against each temporary skeletal pose using the character's current artwork state. Preview framing is shared across the strip; bone visibility follows the animation editor's current view settings, with bones forced on for unskinned characters.

The optional **Show previous pose** setting is editor-only. The immediately preceding keyframe is rendered as a faint noninteractive skeleton beneath the selected pose, with no wraparound at the first keyframe.

---

## 9. Structural editing and character stability

Core centralizes topology/membership operations such as:

```text
create/delete skeleton
create bone (including skeleton merges)
replace skeletons
plan/preview replacement
create character
adopt skeletons
remove character
restore membership
```

`replace_skeletons()` is the principal structural transaction boundary. A `membership_state` snapshot contains:

```text
character ID/name
artwork
standalone pose + minimal animation assets
skeleton -> parent-character membership
```

Structural changes reconcile standalone poses before committing. Animation V2 Phase 2 keyframes contain persistent root-node and bone references. Integrity validation rejects animation data that no longer matches the owning character rig rather than retaining dangling references. Automatic keyframe retargeting for arbitrary topology edits is not part of Phase 2.

---

## 10. Identity and naming

Persistent structural references use `sm::object_id`. Node, bone, skeleton, and character names are display labels rather than structural identity.

`project::rename()` remains a generic cosmetic rename operation across named project objects. Artwork maintains its own intentional semantic string namespaces, such as frame, slot, appearance, and state names.

---

## 11. Mutable access boundary

Generic mutable project lookup exposes nodes and bones only. Aggregate project objects are reached through const lookup or purpose-specific APIs.

Character semantic data still has focused mutable Core accessors:

```cpp
animation_assets& project::animation_data(character_id);
artwork& project::artwork(character_id);
```

The editor model wraps ordinary changes to these values in undoable replacement commands rather than mutating them directly in normal UI workflows.

The Animation Mode working topology is a separate model-layer edit context. Its node/bone lookup resolves against the detached topology while the session is active.

---

## 12. Cut, copy, and paste

Whole-character copy/paste remains self-contained. It preserves:

- character name (with a copy suffix on paste);
- all member topology and persistent constraints;
- artwork and image resources;
- Default and named poses; and
- minimal Animation V2 assets.

Pasted topology receives fresh IDs. One old-to-new topology-ID map is used to remap artwork bone bindings and pose node-position keys. Empty Animation V2 assets contain no topology references and therefore require no topology remapping.

When paste applies a spatial translation, copied standalone pose node positions are transformed by the same translation so they stay aligned with the pasted rig.

---

## 13. Persistence and integrity

Characters are serialized by Core inside the `.stickman` packaged project. Project format version 8 intentionally removes Animation V1 persistence.

Character semantic persistence currently includes:

- character ID and name;
- member skeleton IDs;
- artwork metadata/resources; and
- animation data containing standalone poses plus optional minimal Animation V2 IDs/names.

Topology remains project-level rather than duplicated in character records.

`project::validate_integrity()` combines project-wide structural ID uniqueness, constraint validation, membership consistency, standalone-pose validation, and Animation V2 keyframe validation. Keyframes must refer only to the owning character rig and must contain the complete root/bone state expected by the current Phase 2 full-rig pose domain.

---

## 14. Design principle

The current architecture is:

> **Topology owns persistent skeletal geometry; a character owns the stable semantic boundary over one or more topology components; Animation Mode owns a temporary detached posing context.**

That division lets ordinary project editing, standalone poses, artwork authoring, and future Animation 2.0 data share the character boundary without letting temporary frame posing mutate the persistent project.
