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

As the semantic model grows, it would be reasonable to tighten this boundary so invariants such as character-scoped animation references cannot be bypassed accidentally. This is a hardening opportunity rather than evidence that the current architecture needs replacement.

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

Whole-character clipboard copy is partially self-contained today.

Current copy/paste preserves:

- character name (with `copy` suffix on paste);
- all member topology;
- character root bone;
- artwork and image resources.

On paste, fresh skeleton/node/bone IDs are generated. Artwork bone bindings and the character root bone are remapped to those fresh IDs.

### Current missing piece: animation

Whole-character copy/paste does **not** currently preserve `animation_assets`.

The clipboard's temporary Core resource package copies artwork but never assigns the source character's animation data, and `mdl::project::paste_character()` creates membership state with empty animation data. Core subsequently creates a fresh Default pose for the pasted rig.

Thus a copied animated character keeps its artwork but loses named poses and animations.

This should be fixed before adding many more action types because every additional action payload increases the amount of ID-remapping logic required for a correct character copy.

The preferred direction is a centralized animation remapper that receives the old->new topology-ID map and rewrites every pose/action reference through one exhaustive action visitor.

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

Core owns package/JSON/image serialization so the Qt editor and future runtimes do not need separate persistence implementations.

---

## 15. Current character-level integrity strengths

Several previously risky areas are now well defined:

- global persistent IDs replaced load-bearing node/bone names;
- mutable generic project lookup is limited to node/bone editing;
- character membership is bidirectional and validated;
- the character can own multiple skeletons;
- character root is an explicit persistent bone designation;
- topology replacement is planned/staged before live mutation;
- undo snapshots character membership plus artwork/animation semantics;
- artwork bone IDs are remapped for identity-preserving replacement;
- animation actions that would dangle are identified centrally and removed atomically;
- Core package persistence includes character artwork and animation.

These pieces make the current model coherent enough that the next work should be hardening, not another ownership refactor.

---

## 16. Character-level gaps to fix before broad action expansion

### 16.1 Pose reconciliation during rig evolution

This is the largest current mismatch between “character as stable authored boundary” and animation behavior.

The character may validly gain/lose rig nodes, but stored poses are not automatically reconciled with those membership changes. Since pose compatibility requires an exact node set, supported structural edits can strand existing poses/animations.

Core needs an explicit policy and API for reconciling Default/named poses when character membership changes.

### 16.2 Character-scoped animation validation

Animation validation currently checks that action references resolve in the project topology, not that they belong to the animation's owning character.

The Core invariant should become:

> Every persistent topology reference stored by a character animation resolves to an object in that character's rig and satisfies the structural requirements of its action type.

### 16.3 Character copy must include animation

As described above, copied characters are currently missing their animation assets. Implementing this now is much cheaper than implementing it after the action variant grows.

### 16.4 Consider narrowing raw mutable semantic access

`project::animation_data()` and `project::artwork()` expose mutable references. The model uses them responsibly through commands, but a future Core API could provide mutation methods/transactions that validate before commit.

This would make the project's semantic invariants harder to bypass accidentally.

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

That division is working. Future features should build on it rather than collapsing character and skeleton back into the same concept.
