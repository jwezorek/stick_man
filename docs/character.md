# `sm::character`: Current Architecture

**Reviewed for Animation 2.0 Phase 1 — September 26, 2026**

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

## 6. Animation V2 Phase 1 assets

Animation 2.0 Phase 1 deliberately persists only an empty animation identity:

```cpp
struct animation {
    object_id id;
    std::string name;
};
```

An animation currently has no pose domain, keyframes, transitions, evaluator, motion paths, action layers, or serialized frame data. Creating, renaming, duplicating, or deleting an animation is an ordinary persistent project edit.

The animation collection is omitted from the character's animation JSON when it is empty. When animations exist, only their IDs and names are persisted.

See `animation.md` for the future Animation 2.0 design. `old_Animation.md` is historical documentation of the removed action-based system only.

---

## 7. Animation Mode editing session

Opening an animation does not edit the persistent character topology directly. The editor model creates an Animation Mode session containing:

```text
animation asset identity
character identity
detached working topology
session undo stack
session redo stack
```

The working topology is copied from every skeleton in the character's current rig, including its persistent constraints. It begins from the character's current pose; no standalone pose is applied.

For Phase 1 the entire character rig is the temporary poseable region. The ordinary Selection Tool manipulates the detached topology, so the existing rigid and IK posing behavior is reused without a second animation-specific selection implementation.

The session is intentionally disposable. Temporary node movement and pin changes:

- never mutate the persistent `sm::project` topology;
- never enter document undo/redo history;
- never advance project dirty state;
- never become keyframes; and
- are never serialized.

Saving while Animation Mode is active serializes `sm::project` only, so the scratch pose cannot leak into the project package.

---

## 8. Session undo/redo and pins

Animation Mode has its own undo and redo stacks. Normal Edit -> Undo/Redo actions route to those stacks while the session is active and return to the unchanged document history when the session ends.

Node pins are currently editor/session posing aids. In Animation Mode pin toggles are recorded in the session history and are restored/discarded on exit. They are not persistent constraints or Animation V2 transition pins.

Persistent rotation and rigid-triangle constraints are copied into the detached topology so they continue to influence IK, but constraint creation, editing, deletion, and adornment dragging are disabled in Animation Mode.

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

Structural changes reconcile standalone poses before committing. Animation V2 Phase 1 assets contain no topology references, so no animation dependency cascade or retargeting is required.

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

`project::validate_integrity()` combines project-wide ID uniqueness, constraint validation, membership consistency, and character-scoped standalone-pose validation. Animation V2 Phase 1 has no topology-bearing animation data to validate beyond asset identity uniqueness.

---

## 14. Design principle

The current architecture is:

> **Topology owns persistent skeletal geometry; a character owns the stable semantic boundary over one or more topology components; Animation Mode owns a temporary detached posing context.**

That division lets ordinary project editing, standalone poses, artwork authoring, and future Animation 2.0 data share the character boundary without letting temporary frame posing mutate the persistent project.
