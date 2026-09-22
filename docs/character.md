# `sm::character`: Current Architecture

**Reviewed against the September 22, 2026 source selection**

## 1. Purpose

A character is the stable authored boundary that turns one or more skeleton components into a single animation/artwork object.

A skeleton is topology. A character is ownership and semantics.

The current character owns:

```text
identity and display name
rig membership
character root bone
artwork / appearances
poses and animations
```

Loose skeletons remain valid project objects and are useful while constructing geometry before promoting/adopting it into a character.

---

## 2. Core representation

The current Core shape is approximately:

```cpp
class character {
    object_id id_;
    std::string name_;
    project& owner_;
    sm::rig rig_;
    object_id character_root_bone_;
    sm::artwork artwork_;
    animation_assets animation_data_;
};
```

`sm::rig` is a character-owned membership view containing persistent skeleton IDs. It does not duplicate the skeleton geometry.

The authoritative nodes, bones, and skeletons live in `sm::project::topology()`.

This separation is fundamental:

```text
project topology
    owns nodes/bones/skeletons

character
    owns membership + character semantics
```

---

## 3. Why character and skeleton are separate

A character can legitimately contain multiple disconnected skeletons.

Examples include:

- a conventional body plus detached eye/face controls;
- floating accessories;
- intentionally disconnected animated pieces;
- a skeleton split by an edit where both surviving components should remain part of the same character.

Therefore “one skeleton == one character” would make ordinary editing operations change user-level identity.

The character survives changes in the number/connectivity of its member skeletons.

---

## 4. Rig membership

`sm::rig` stores the IDs of skeletons owned by the character.

Each owned skeleton also carries a non-owning parent-character relationship back to the character. `project::has_consistent_membership()` checks both directions.

A valid character:

- contains at least one skeleton;
- contains no duplicate skeleton membership;
- owns only live skeletons from the project topology;
- agrees with each member skeleton's parent-character link;
- has a character root bone that, when non-nil, belongs to that character.

Loose skeletons have no parent character and may later be adopted.

---

## 5. Character root bone

A character has a persistent `character_root_bone`.

This is a **bone**, not a node.

The root bone supplies an oriented 2D frame:

```text
origin      = root/parent node of the bone
orientation = root -> tip direction
```

Animation uses that designation for two symbolic translation frames:

- **Animation Root** — the root-bone frame in the animation base pose;
- **Character Root** — the root-bone frame in the currently evaluated intermediate topology.

The project repairs the root-bone designation after structural changes if the previous root is removed. Setting the character root is a character-level semantic edit rather than renaming/reidentifying a skeleton.

Because actions refer symbolically to “Animation Root” or “Character Root,” changing the character's designated root bone intentionally changes what those symbolic references mean. A Bone-relative action, by contrast, stores a specific reference-bone ID.

---

## 6. Character-owned artwork

Every character owns one `sm::artwork` value.

That artwork contains:

- logical image frames;
- semantic slot definitions bound to bones;
- slot state vocabularies;
- appearances;
- appearance-slot state mappings;
- local sprite transforms.

Artwork bone references are remapped when a topology replacement deliberately preserves a bone while assigning it a fresh ID.

Artwork can also remain temporarily unresolved when its bound bone disappears. Rendering checks that a slot's bone both exists and belongs to the owning character; unresolved slots are skipped rather than silently rebound.

See `Appearances.md` for the full model.

---

## 7. Character-owned animation

Every character owns `animation_assets` containing:

- the built-in Default pose;
- named poses;
- animations and their actions.

Actions use persistent node/bone/skeleton IDs into the character rig.

Ordinary structural editor operations calculate which actions depend on topology objects that will be removed. Those actions are deleted atomically with the topology edit after editor confirmation, and undo restores them with the rest of the character membership snapshot.

This means normal editor operations do not intentionally leave dangling animation action references.

See `Animation.md` for evaluation and action semantics.

---

## 8. Structural editing and character stability

The Core project centralizes operations that can change topology or membership.

Important operations include:

```text
create/delete skeleton
create bone (including skeleton merges)
replace skeletons
plan/preview replacement
create character
adopt skeletons
remove character
set character root bone
restore membership
```

`replace_skeletons()` is the major structural transaction boundary used by editor operations. It stages/validates replacements before erasing live topology, then restores character membership metadata around the replacement.

A `membership_state` snapshot can include:

```text
character ID/name
character root bone
artwork
animation assets
skeleton -> parent-character membership
```

That snapshot makes topology edits and undo semantic rather than merely geometric.

---

## 9. Deletion, splits, and merges

### 9.1 Deleting topology

When a structural edit removes persistent node/bone/skeleton identities, Core calculates `topology_edit_effects`.

For animation, dependent actions are removed rather than retained with dangling IDs.

Artwork has different semantics: an artwork slot whose bone no longer resolves may remain as an unresolved authored binding and is skipped at render time.

### 9.2 Splitting skeletons

Deleting a bone can turn one skeleton into multiple skeletons. This does not inherently destroy the character because the character's rig may own multiple components.

### 9.3 Merging skeletons

Creating a bone can merge previously separate skeleton components. The project owns the membership bookkeeping and action-dependency effects associated with disappearing skeleton IDs.

Character identity is therefore not tied to one particular skeleton object surviving forever.

---

## 10. Identity and naming

Persistent structural references use `sm::object_id`.

Node, bone, skeleton, and character display names are labels rather than structural identity.

`project::rename()` is a generic cosmetic rename operation across named project entities supported by the project's object lookup.

Artwork introduces its own intentional semantic string namespaces (frame names, slot names, appearance names, state names). Those are character-local artwork semantics, not substitutes for topology object identity.

---

## 11. Mutable access boundary

The project intentionally limits generic mutable object lookup to nodes and bones. Aggregate objects are exposed through const lookup/membership APIs instead of handing callers arbitrary mutable topology/character references.

There are still direct character-data mutation accessors:

```cpp
animation_assets& project::animation_data(character_id);
artwork& project::artwork(character_id);
```

The editor model wraps ordinary animation/artwork changes in undoable snapshot-style edit commands, but Core itself does not make those mutable references transactional.

The editor model does not mutate those references in place during ordinary UI edits: it copies the semantic value, applies the requested edit, validates the candidate when appropriate, and records an undoable replacement command.

---

## 12. Editor character workflow

The editor supports both loose skeleton construction and character-centric authoring.

A typical workflow is:

```text
construct one or more loose skeletons
        ->
Make Character / adopt skeletons
        ->
choose/repair character root bone
        ->
author artwork and appearances
        ->
capture poses
        ->
author animations
```

Character selection is visually distinct from raw skeleton editing, and character-owned panes such as Artwork and Animation operate against this stable boundary.

---

## 13. Cut, copy, and paste

Whole-character clipboard copy/paste is self-contained at the character level.

Current copy/paste preserves:

- the character name (with a `copy`/`copy N` suffix on paste);
- all member topology;
- the character root bone;
- artwork, appearances, and image/frame resources;
- the Default and named poses;
- animations, layers, actions, motion paths, pins, and action timing/easing data.

The clipboard embeds a temporary serialized Core package for the character-owned resources so the editor does not need to copy packed image resources manually.

On paste, the new character and all topology objects receive fresh identities. A single old-to-new topology-ID map is then used to:

- remap artwork bone bindings;
- remap the character root bone;
- remap every pose node-position key;
- remap every persistent node/bone/skeleton reference stored by animation actions.

`remap_animation_assets()` uses the same exhaustive persistent-reference visitor that dependency discovery uses. Pose/animation/action IDs themselves are preserved inside the copied animation assets; only topology references are rewritten.

When paste applies a spatial translation rather than using Paste in Place, the copied pose positions are transformed by that same translation so animation base/named poses remain spatially aligned with the pasted rig.

The complete character paste is one undoable command; redo restores the same pasted character identity. Ordinary non-character topology copy/paste continues to create loose skeleton geometry rather than implicitly adopting it into the currently selected character.

---

## 14. Persistence

Characters are serialized by Core inside the `.stickman` packaged project.

The current project JSON format version is 6. The loader also contains compatibility paths for versions 4 and 5.

Current character semantic persistence includes:

- character ID and name;
- member skeleton IDs;
- character root bone;
- artwork metadata/resources;
- animation data.

Topology remains a project-level structure rather than being duplicated inside each character record.

Core owns package/JSON/image serialization so the Qt editor and runtime consumers do not need separate persistence implementations.

---

## 15. Character-level integrity

The current character/project boundary enforces several semantic invariants:

- global persistent IDs, rather than display names, carry structural identity;
- generic mutable project lookup is limited to node/bone editing;
- character membership is bidirectional and validated;
- a character may own multiple disconnected skeletons;
- the character root is an explicit persistent bone designation;
- topology replacement is planned and semantically validated before live mutation;
- undo snapshots character membership together with artwork and animation assets;
- artwork bone IDs are remapped when a preserved semantic bone receives a fresh ID;
- animation actions whose dependencies are truly removed are identified centrally and deleted atomically with the topology edit;
- animation pose membership is reconciled after supported structural/membership edits;
- animation validation is scoped to the owning character rig;
- whole-character copy/paste remaps both artwork and animation references;
- Core package persistence includes both character artwork and animation data.

`project::validate_integrity()` combines object-index uniqueness, membership consistency, and character-scoped animation validation.

---

## 16. Rig changes, poses, and animation integrity

Character membership can change without changing the character's user-level identity, so the project normalizes animation state as part of structural transactions.

### 16.1 Pose reconciliation

`reconcile_animation_poses()` applies the current membership policy:

- Default removes nodes that no longer belong to the character;
- Default adds newly introduced member nodes at their current world positions;
- retained Default entries keep their existing authored positions;
- named poses drop nodes that no longer belong to the character;
- named poses are not automatically extended when new member nodes are introduced.

Because pose compatibility is an exact node-set comparison, a named pose may therefore become incomplete when the rig grows. Applying such a pose or using it as an Animation Mode base pose is rejected until it is updated/recreated. This is current authored-pose behavior rather than a dangling-reference condition.

### 16.2 Character-scoped animation validation

`animation_assets::validate(topology, rig_skeletons, character_root_bone)` requires pose/action topology references to resolve inside the owning character rig. It also checks same-skeleton IK constraints, character-root requirements, and reference-frame ordering.

`mdl::project::edit_animation_data()` validates a copied candidate before committing it as an undoable edit. Structural operations validate a detached candidate character/topology state before committing live changes.

Changing `character_root_bone` is also validated before commit because Character Root references and their ordering constraints are defined in terms of that designation.

### 16.3 Structural deletion and action cascades

Before an edit removes persistent topology identities, Core computes `topology_edit_effects`. Any action whose active persistent dependency is being removed is included in the semantic cascade and removed atomically with the topology edit. Undo restores the topology and affected animation data together.

No heuristic retargeting is performed.

---

## 17. Design principle

The current architecture can be summarized as:

> **Topology owns geometry and persistent structural objects; a character owns the stable semantic boundary over one or more topology components.**

The character is therefore the right home for:

```text
rig membership
root-frame semantics
artwork / appearances
poses
animations
```

while nodes, bones, and skeleton connectivity remain in the project topology.

The current project, artwork, animation, clipboard, and undo implementations all rely on that division: topology supplies structural geometry; the character supplies persistent semantic ownership over it.
