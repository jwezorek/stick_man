# stick_man Artwork and Appearances Design

Architecture, character-owned artwork, named sprite frames, appearance/state resolution, sprite animation, editor UX, package resources, and phased implementation plan.

Review draft — updated September 12, 2026

## 1. Executive Summary

The goal is to add bone-driven bitmap artwork to `stick_man` without weakening the character/rig ownership model that now exists in Core.

The current implementation already has the correct durable semantic owner: `sm::project` owns topology and characters; topology owns skeletons, nodes, and bones; and each `sm::character` owns a rig whose skeleton IDs refer back into project topology. Artwork should follow the durable character rather than any individual skeleton component.

Conceptually:

```text
project
├── topology
│   └── skeletons / nodes / bones
└── characters
    └── character
        ├── rig
        ├── artwork
        │   ├── sprite atlas / sprite-frame library
        │   └── appearances
        └── animation                  (future character effort)
```

Each character directly owns one `sm::artwork`. Artwork owns:

- one logical sprite atlas, which is a character-local mapping from **unique sprite-frame names** to image resources; and
- zero or more appearances.

Sprite-frame names are intentionally load-bearing resource keys. A sprite does **not** need an opaque ID merely to avoid a string reference. The atlas already is a symbol table:

```text
"human_eye_open"   -> sprite frame
"human_eye_half"   -> sprite frame
"human_eye_closed" -> sprite frame
```

Bones expose semantic string-valued sprite slots. A static appearance can therefore be understood as mapping slot names to sprite-frame names:

```text
bone slot "eyes"
        |
        v
Human appearance: "eyes" -> "human_eye_open"
        |
        v
character sprite atlas: "human_eye_open" -> pixels
```

To support sprite-based animation without tying an animation to one concrete appearance, artwork also owns an explicit **semantic state vocabulary for each slot**. A slot is therefore more than a string used by one appearance: it is part of the character's visual interface.

For example:

```text
slot "eyes"
    valid states:
        default
        open
        half
        closed
```

All appearances of that character share this slot/state schema. An appearance does not invent its own states; it maps the character-defined states to concrete frame names:

```text
(slot, semantic state) -> sprite frame
```

For example, the animation says `eyes = closed`; the Human appearance maps `(eyes, closed)` to `human_eye_closed`; the Robot appearance maps the same semantic state to `robot_eye_closed`. Animation owns timing and semantic state. Appearance owns concrete imagery. Switching appearances does not change the animation state.

Static artwork is simply the special case in which a slot uses only its mandatory `default` state.

Appearance-item anchor, translation/rotation/scale, and draw order are **state-independent**. They position one logical visual layer for a slot, and every semantic state of that item shares the same placement. Individual sprite frames carry a registration origin so frames of different pixel sizes/crops still align. A future optional per-state transform delta may be added if actual authoring experience shows it is useful, but it is not part of the initial model.

The design also keeps these earlier decisions:

- artwork is owned directly by a character and is never a project-level shared resource;
- different characters do not share sprite atlases by reference;
- a bone's sprite slot is semantic and load-bearing, while ordinary node/bone names remain cosmetic;
- appearance items use a bone-local transform with explicit root/tip anchoring;
- multiple appearance items may target the same slot, allowing layered pieces such as a limb plus a joint cover;
- draw order is explicit and user-editable;
- node sprite binding is not required initially;
- sprite frames have a registration origin so differently sized animation frames do not jump when changed;
- atlas packing is a private Core persistence/resource detail, not semantic identity;
- active appearance is editor/runtime-instance state, not mutable skeleton state; and
- Core remains renderer-independent and does not expose Qt, SDL, GPU, JSON-library, stb, or miniz implementation types through public APIs.

When `artwork_` is added to `sm::character`, character-destruction undo must be extended at the same time so that an operation which actually destroys a character preserves and restores its owned artwork. This is an implementation requirement of adding character-owned artwork, not a pre-existing bug or a separate prerequisite refactor.

## 2. Current-Code Constraints and Required Integration Changes

This design is written against the current character-refactor implementation rather than the older pre-character architecture.

### 2.1 Existing ownership already matches artwork

The current Core shape is approximately:

```cpp
class character {
    const object_id id_;
    std::string name_;
    std::reference_wrapper<project> owner_;
    sm::rig rig_;
};
```

Artwork should extend that directly:

```cpp
class character {
    const object_id id_;
    std::string name_;
    std::reference_wrapper<project> owner_;
    sm::rig rig_;
    sm::artwork artwork_;
};
```

Artwork must not be stored on `sm::skeleton`. Skeletons are deliberately topology components whose identities may change under split/merge/edit operations. The character is the persistent semantic object specifically intended to own long-lived resources such as artwork and future animation.

Likewise, there should be no project-level atlas table whose entries are referenced by characters. Each character owns its own artwork and all image resources reachable from it.

### 2.2 Sprite-frame names are resource identity; project object IDs remain a different concept

The editor currently treats `sm::object_id` as a project-wide namespace for live model objects such as nodes, bones, skeletons, and characters. Artwork should not automatically enlarge that namespace.

A sprite frame is not a project object handle. Its unique name within one character's artwork is sufficient identity:

```cpp
sprite_atlas["human_eye_closed"] -> sprite_frame
```

Consequences:

- sprite-frame names must be unique within one character's atlas;
- renaming a sprite frame is a semantic rename and must update all appearance mappings that reference the old name;
- two different characters may both contain a frame named `head` without conflict;
- sprite frames do not participate in `sm::project::get()`;
- sprite frames do not participate in the project object index;
- sprite frames are not `mdl::handle`s; and
- copying a character may preserve its sprite-frame names exactly because the destination has a separate namespace.

Appearance identity is different. Appearances are user-visible mutable aggregates whose names are cosmetic. It is useful for an appearance to retain a stable character-local opaque identity across rename and editing. If `object_id` is reused as the representation for an appearance ID, that ID is **character/artwork-scoped metadata**, not a live project object ID and not an `mdl::handle`.

Appearance items do not require persistent opaque IDs in the initial design. They are values owned by an appearance. Tool-local editor selection can refer transiently to an item and clear/re-resolve that selection when the item's container is structurally changed.

### 2.3 Character destruction/undo must preserve full character payload

The current topology replacement and undo machinery is correct for the character model that exists today. A character currently has no owned semantic payload beyond its identity/name and rig membership, so when topology replacement destroys a character and undo later restores it, Core can reconstruct the complete character from the existing lightweight membership state.

Adding `artwork_` to `sm::character` changes that requirement. From that point onward, an undoable operation that actually destroys a character must retain and restore that character's owned artwork as part of its undo state. The same rule will later apply to other character-owned payload such as animation or poses.

This does **not** mean ordinary membership snapshots should begin copying artwork. Membership snapshots are used by routine topology edits that split, merge, replace, or reassign skeletons while the character itself survives. They should remain lightweight and continue to record only the state needed to restore skeleton-to-character membership.

The distinction is:

```text
ordinary topology/membership change
    character survives
    -> preserve topology and membership state only

character lifetime actually ends
    for example, deleting its last skeleton or deleting the whole character
    -> undo state must also retain the character's owned payload
       so undo restores the same character, including artwork
```

The existing `replacement_plan::deleted_character_ids` already identifies the important case in the current replacement machinery: an affected character will no longer survive the replacement. That can be used by the artwork implementation to determine which character payload must be retained for undo.

This is **not a pre-existing bug or a prerequisite refactor**. It is a requirement that must be handled as part of Phase 1 when `artwork_` is added to `sm::character`.

### 2.4 Character-owned artwork may be mutable below the project boundary

The reason skeleton, bone, node, and rig mutation is tightly controlled by `sm::project` is that those objects participate in project-wide identity and topology invariants. Nodes, bones, skeletons, and characters use project-level IDs; topology edits can split and merge skeletons; and skeleton/character membership must remain consistent on both sides.

Artwork does not need the same restriction. Under this design, the character is the project-owned object. Sprite-frame names, appearances, appearance items, and other artwork data are scoped to that character rather than participating in the project's live object-ID namespace.

It is therefore reasonable for `sm::project` to expose mutable character access, while `sm::character` itself controls which owned subobjects may be mutated directly. Conceptually:

```cpp
class project {
public:
    const character& character(object_id id) const;
    character& character(object_id id);
};

class character {
public:
    const sm::rig& rig() const noexcept;       // membership remains project-controlled

    const sm::artwork& artwork() const noexcept;
    sm::artwork& artwork() noexcept;           // character-local mutable state
};
```

The important distinction is that mutable access to a character does **not** imply unrestricted mutable access to its rig. Rig membership still crosses project/topology boundaries and must remain under `sm::project` control.

By contrast, mutations that are entirely internal to artwork can naturally live on `sm::artwork` itself. `sm::artwork` should expose semantic operations that preserve its own local invariants rather than raw mutable access to internal maps/vectors. For example, exact names TBD:

```cpp
artwork.create_appearance(name);
artwork.rename_appearance(appearance_id, name);
artwork.delete_appearance(appearance_id);

artwork.import_sprite_frame(frame_name, rgba8_image, origin);
artwork.rename_sprite_frame(old_name, new_name);
artwork.delete_sprite_frame(frame_name);

artwork.add_appearance_item(appearance_id, item);
artwork.update_appearance_item(appearance_id, item_index, item);
artwork.delete_appearance_item(appearance_id, item_index);
```

An operation such as `rename_sprite_frame()` is artwork-local even though it may touch several internal structures: it can rename the atlas entry and rewrite every appearance mapping that refers to that frame while keeping the artwork valid.

Operations that cross the artwork/topology boundary should remain project-controlled. Sprite-slot changes are the clearest example. A slot is declared on a bone but is referenced by appearances and, later, by sprite-animation events. A semantic slot rename therefore needs one operation that can update all affected character state consistently, conceptually:

```cpp
project.rename_sprite_slot(character_id, old_slot, new_slot);
```

That operation may update bone slot declarations, artwork mappings, and future animation slot references. The editor should not need to know every Core subsystem that happens to refer to a slot name.

`mdl::project` remains responsible for wrapping user-visible mutations in undoable commands and emitting the appropriate editor notifications. The intended layering is therefore:

```text
UI / mdl::project
    undo/redo and editor notifications
        |
        v
sm::project
    project-wide identity/topology operations
    mutable access to project-owned characters
        |
        v
sm::character
    rig exposed read-only
    artwork exposed as character-owned mutable state
        |
        v
sm::artwork
    enforces artwork-local invariants through semantic operations
```

### 2.5 Sprite editing should not be forced into topology selection

The current canvas/model selection path is centered on topology pieces and character selection. Canvas items resolve to `mdl::skel_piece`/project model objects, and existing transform behavior assumes selection means nodes, bones, skeletons, or characters.

A sprite binding/appearance item is not a topology object and does not need to become one merely so the Sprite Transform tool can edit it.

The initial Sprite Transform tool should therefore keep **tool-local appearance-item selection**. Normal project selection remains focused on model/topology objects. When the Sprite Transform tool is left, when the appearance changes, or when the selected item is deleted/reordered incompatibly, its transient selection can be cleared or re-resolved.

This avoids expanding `mdl::handle`, `mdl::selection`, `sm::project::get()`, and every existing selection/property path simply to manipulate artwork values.

## 3. Terminology and Core Semantic Model

### 3.1 Artwork

`sm::artwork` is directly owned by one `sm::character` and contains:

- one logical `sm::sprite_atlas`; and
- zero or more appearances.

The artwork object's lifetime is exactly the character's lifetime.

### 3.2 Sprite atlas

The term **sprite atlas** means the logical character-local sprite-frame library. It is not synonymous with one packed bitmap.

A logical atlas may serialize to several physical **atlas pages**.

```text
logical sprite atlas
    "head"             -> sprite frame
    "eye_open"         -> sprite frame
    "eye_closed"       -> sprite frame
    "left_hand"        -> sprite frame

serialized resource representation
    page-0.png
    page-1.png
    ...
```

Atlas-page count, packing rectangles, and page assignment are generated resource metadata and are not semantic identity.

### 3.3 Sprite frame

What the editor informally calls a sprite is more precisely a **sprite frame**: one concrete bitmap image that can be selected by an appearance for a particular slot/state.

A frame has no opaque ID. Its name is the key in the character's sprite atlas.

Conceptually:

```cpp
struct sprite_frame {
    rgba8_image image;
    point origin{}; // registration offset in frame-local pixel units; see section 6
};

using sprite_atlas = map<string, sprite_frame>;
```

The exact container/resource-storage types are implementation choices. In particular, loaded package imagery may internally be backed by decoded atlas pages rather than one allocation per frame. That storage distinction must remain hidden behind Core resource APIs.

### 3.4 Sprite slots

A bone may expose a semantic sprite-slot name:

```cpp
struct bone {
    object_id id;
    std::string name;        // cosmetic display label
    std::string sprite_slot; // semantic artwork interface; empty means no slot
    // ...
};
```

The slot is intentionally load-bearing. It is the interface between the rig, appearances, and sprite-state animation.

A non-empty slot name must be unique among the live bones belonging to one character's rig. Two different characters may use the same slot vocabulary.

Loose skeletons may contain arbitrary slot names. Operations that establish or extend character membership must validate the resulting character slot namespace:

- Make Character;
- Adopt Skeletons; and
- any future operation that transfers topology into an existing character.

If adopting a skeleton would create two live bones with the same non-empty slot, Core should reject the operation until the conflict is resolved rather than silently choose one.

The bone declares which semantic slot it implements, but the slot's visual state vocabulary is character-owned artwork data rather than bone data. Artwork keeps one slot definition for each known semantic slot; a new definition starts with the mandatory `default` state and may add states such as `open`, `half`, `closed`, or `smile`.

Assigning a previously unseen slot name to a bone should create the corresponding artwork slot definition with `default`. Make Character / Adopt Skeletons should likewise ensure definitions exist for the resulting rig's slots.

A useful property of this split is that artwork can outlive the topology currently satisfying a slot. Deleting the last bone that implements `eyes` does not delete the artwork definition `eyes = {default, open, half, closed}`. Appearance mappings and future animation vocabulary remain intact but unresolved. If a later edit/adoption introduces a bone with that slot again, the artwork resolves again automatically.

### 3.5 Appearance

An appearance is one visual interpretation of a character's **shared slot/state schema**: Human, Robot, Winter Coat, Skeleton, and so on.

An appearance does not define which slots exist and does not define the valid states for a slot. Those are character-level artwork semantics shared by every appearance. The appearance only supplies concrete visual implementations of that schema.

Therefore all appearances of one character have the same semantic slots and the same valid state vocabulary for each slot by definition. An appearance may temporarily have no item for a slot while authoring, or may intentionally render nothing for it, but that does not mean the slot ceases to exist for that appearance.

An appearance owns a list of **appearance items**. Multiple appearance items may refer to the same slot. This is required for layered cutout artwork: for example, one lower-leg slot may drive the leg image plus a separate knee-cover or highlight image at another draw order.

An appearance item contains:

- the semantic slot it follows;
- mappings from **declared states of that slot** to concrete sprite-frame names;
- one root/tip anchor shared by all of its states;
- one local translation/rotation/scale shared by all of its states; and
- one explicit draw order shared by all of its states.

The item itself does not require a persistent opaque ID in the initial design.

### 3.6 Semantic sprite states and slot definitions

Sprite state names such as `default`, `open`, `half`, `closed`, `smile`, or `hidden` are intentionally symbolic strings. They are **not concrete sprite-frame names**.

The set of valid states is explicit and belongs to the character's artwork **per slot**. Semantically it is a small user-defined enum:

```text
eyes = { default, open, half, closed }
mouth = { default, closed, smile, open_1, open_2 }
torso = { default }
```

`default` is mandatory for every slot definition. Other state names are user-authored and unique within that slot.

An appearance cannot create an arbitrary new state simply by using a new string. Every state key in an appearance item must be declared by the corresponding artwork slot definition. This gives the animation system an authoritative vocabulary independent of whichever appearance happens to be active.

Conceptually Core should be able to answer:

```cpp
artwork.states_for_slot("eyes")
    -> ["default", "open", "half", "closed"]
```

The animation editor can use that list directly when the user inserts a sprite-state event. Live animation state is likewise a validated `(slot, state)` pair rather than an unconstrained pair of arbitrary strings.

The pair:

```text
(slot, state)
```

is therefore the semantic key used by sprite animation. Each appearance item maps declared states to concrete frame names.

Every appearance item must define a `default` mapping. The target of a state mapping is either:

- a sprite-frame name in the owning character's atlas; or
- explicit `none`, meaning this item is hidden in that state.

An appearance item is allowed to omit a non-default declared state. If animation requests such a state, that item falls back to its `default` mapping. This lets, for example, Human have distinct `open` / `half` / `closed` eye frames while Robot deliberately reuses its default image for `half`.

Example:

```text
Character artwork slot definition:
    eyes = { default, open, half, closed }

Animation semantic state:
    eyes = half

Human appearance item:
    default -> human_eye_open
    open    -> human_eye_open
    half    -> human_eye_half
    closed  -> human_eye_closed

Robot appearance item:
    default -> robot_eye_open
    open    -> robot_eye_open
    closed  -> robot_eye_closed
    // no explicit half mapping: fall back to default
```

State definitions are more durable than the bone currently satisfying the slot. If the `eyes` bone is temporarily removed, the `eyes` state vocabulary remains part of the character artwork along with its appearance mappings and future animation references.

## 4. Proposed Core Data Model

The exact C++ types should follow existing Core style. Conceptually:

```cpp
struct sprite_transform {
    point translation{};
    float rotation = 0.0f; // radians in Core and JSON
    point scale{1.0f, 1.0f};
};

enum class bone_anchor {
    root,
    tip
};

struct sprite_frame {
    image_resource image;

    // Registration offset from geometric image center, expressed in
    // frame-local pixel units: +X right, +Y up. {0,0} means center.
    point origin{};
};

struct sprite_atlas {
    // Names are unique, load-bearing keys within this character only.
    map<string, sprite_frame> frames;
};

struct slot_definition {
    // "default" is always present; remaining names are user-defined.
    vector<string> states;
};

using frame_target = optional<string>; // nullopt = explicitly hidden

struct appearance_item {
    std::string slot;

    // Must contain "default". Every key must be declared by the
    // artwork slot_definition for this slot.
    map<string, frame_target> states;

    // These properties belong to the item/slot layer, not to a state.
    // Switching semantic state changes only the selected frame target.
    bone_anchor anchor = bone_anchor::root;
    sprite_transform transform;
    int draw_order = 0; // lower draws first
};

struct appearance {
    std::string name;   // unique, load-bearing within the owning artwork
    std::vector<appearance_item> items;
};

struct artwork {
    sprite_atlas atlas;

    // Character-level visual interface shared by every appearance.
    map<string, slot_definition> slots;

    std::vector<appearance> appearances;
};
```

The character owns it directly:

```cpp
class character {
    const object_id id_;
    std::string name_;
    std::reference_wrapper<project> owner_;
    sm::rig rig_;
    sm::artwork artwork_;
};
```

Important invariants:

1. An appearance belongs to exactly one character's artwork.
2. Every semantic slot known to artwork has exactly one character-level slot definition, and every slot definition contains `default`.
3. Every appearance item names an artwork slot definition; appearances do not invent slots.
4. Every state key in an appearance item is declared by that slot definition; appearances do not invent states.
5. Every non-null appearance state target names a frame in that same character's atlas.
6. Sprite-frame names are unique within the atlas.
7. Non-empty live bone slot names are unique within one character rig.
8. Every appearance item contains a `default` state mapping.
9. Anchor, T/R/S, and draw order are properties of an appearance item and are shared across all of that item's semantic states.
10. Multiple appearance items may target the same slot.
11. Appearance-item draw order is deterministic within an appearance.
12. Atlas page/rectangle placement is not present in the semantic `sprite_frame` identity.
13. Artwork cannot contain a live reference to another character's resources.

## 5. Static and Animated Resolution

### 5.1 Static appearance resolution

Without sprite animation, the current semantic state of every slot is `default`.

For each appearance item:

```text
item.slot
   |
   v
find live bone in character rig with matching sprite_slot
   |
   v
item.states["default"]
   |
   +--> none: do not render item
   |
   v
sprite-frame name
   |
   v
character.artwork.atlas[name]
   |
   v
pixels + frame origin
```

Thus the common static case remains conceptually as simple as:

```text
slot -> sprite-frame name
```

The explicit state map merely leaves room for animation without introducing a second binding model later. Its keys come from the character-level slot definition; the active appearance cannot change the set of valid states.

### 5.2 Sprite-state animation events

The existing `sm::animation` already has a timeline of event variants. Today the event variant contains skeletal rotation and translation events. Sprite animation should extend that same timeline with a discrete semantic state event rather than introduce a second independent animation clock.

Conceptually:

```cpp
struct sprite_state {
    std::string slot;
    std::string state;
};
```

The event contains **no concrete sprite-frame name** and normally needs no duration. Its `state` must be one of the states declared by the character artwork definition for `slot`. Once a `sprite_state` event occurs for a slot, that state remains active until another state event for the same slot occurs.

If no event has yet occurred for a slot, its current state is `default`.

The animation editor should therefore populate its state selector from the artwork schema rather than accept arbitrary free-form strings, for example:

```text
Slot:  [ eyes   v ]
State: [ closed v ]   // populated from artwork.states_for_slot("eyes")
```

Example blink:

```text
0 ms      eyes = open
80 ms     eyes = half
120 ms    eyes = closed
180 ms    eyes = half
220 ms    eyes = open
```

At 120 ms:

```text
animation      -> (eyes, closed)
Human          -> human_eye_closed
Robot          -> robot_eye_closed
Skeleton       -> skull_eye_closed, or default if no closed mapping exists
```

The persisted animation therefore remains appearance-independent.

### 5.3 Why timing belongs to animation, not appearance

An alternative design would let each appearance define a named frame sequence such as `blink`. That would make Human and Robot appearances responsible for animation timing, which creates ambiguity when animations synchronize sprite changes with skeletal motion or when the active appearance changes mid-animation.

The initial design instead uses a clean separation:

```text
animation  = when a semantic state changes
appearance = what concrete image represents that state
```

An appearance may map several semantic states to the same frame if it intentionally has fewer unique frames.

A future editor convenience such as “cycle these states every N ms” may generate ordinary `sprite_state` timeline events. A separate persisted `sprite_cycle` event is not required initially.

### 5.4 Switching appearances during animation

The current sprite state is part of animation-instance evaluation, not appearance state.

If an animation is currently at:

```text
eyes  = closed
mouth = open_2
```

and the runtime switches from Human to Robot, the same semantic state continues to apply. The renderer immediately resolves `(eyes, closed)` and `(mouth, open_2)` through Robot instead of Human.

No animation restart or state conversion is required.

### 5.5 Multiple appearance items on one slot

All items targeting one slot observe the same semantic slot state. Each item resolves that state independently and falls back to its own `default` mapping when necessary.

This supports layered artwork naturally:

```text
slot: lower_leg_l

item A (leg art)
    default -> trouser_leg

item B (joint cover)
    default -> knee_cover
    hidden  -> none
```

If future use cases require several independently animated semantic channels attached to exactly the same bone transform, the design can later allow multiple declared slots on a bone. The initial model keeps the current simpler zero-or-one slot property per bone.

## 6. Transform, Registration, and Image Coordinate Semantics

### 6.1 Canonical bone-local orientation

A stored appearance-item transform must be independent of the pose in which it was authored.

```text
              +Y
               ^
               |
root node o----+-------------------------o tip node
               |
               +------------------------> +X

+X = root -> tip
rotation = 0 radians when aligned with +X
```

The binding's bone-local orientation is always root-to-tip.

### 6.2 Root and tip anchors

Every appearance item chooses a root or tip anchor.

```text
root anchor origin = current root-node position
tip anchor origin  = current tip-node position
orientation        = current bone angle, root -> tip
```

Choosing tip changes the origin only. It does not reverse local axes.

This keeps joint-position artwork possible without node bindings and makes distal pieces such as hands/feet follow the correct endpoint when bone length changes.

### 6.3 Per-item, state-independent transform

Anchor, transform, and draw order belong to the appearance item as a whole. They are **not** keyed by semantic state.

Changing:

```text
eyes = open
```

to:

```text
eyes = closed
```

changes only which sprite frame the appearance item resolves. It does not independently translate, rotate, scale, re-anchor, or reorder that item.

The final item transform is:

```text
binding_world = translate(current_anchor_position)
              * rotate(current_bone_angle)

item_world    = binding_world
              * local_translation
              * local_rotation
              * local_scale
```

If an appearance uses several layered items on the same slot, each item has its own transform/anchor/draw-order, but each item's transform remains shared by all semantic states of that item.

Different state frames do **not** need identical pixel dimensions or crops. The sprite-frame registration origin described below aligns each concrete frame to the item's common transform.

Bone length does not implicitly scale artwork.

Rotation is stored in radians in Core and JSON. Editor controls may display degrees.

Non-uniform scale is supported. Negative scale is also valid and intentionally allows mirroring a frame without duplicating the image resource.

If practical authoring later demonstrates a real need for state-specific positional/rotation/scale corrections, the model may add an optional per-state transform delta. That is deliberately deferred; the initial design positions the slot/item once and changes only frame identity as semantic state changes.

### 6.4 Sprite-frame registration origin

Changing from one sprite frame to another must not make an animated feature jump merely because the source images have different pixel dimensions.

Each sprite frame therefore has a registration `origin` in addition to its pixels.

The initial coordinate convention is:

- frame-local origin before registration is the geometric center of the image;
- +X points right;
- +Y points up, matching Core/canvas world orientation;
- origin values are expressed in source-image pixel units;
- `origin = {0,0}` means the geometric image center is the registration point; and
- the registration point is what the appearance-item transform positions.

Thus `eye_open.png` and `eye_closed.png` may have different dimensions while retaining identical apparent placement by adjusting their frame origins.

The default import origin is `{0,0}`.

This is resource metadata, not appearance-specific transform data. Every use of a particular frame shares its intrinsic registration origin; appearance-item translation remains available when one binding needs additional placement adjustment.

### 6.5 Pixel/world scale and Y direction

The initial editor/runtime convention should be explicit:

- one source image pixel corresponds to one Core world/scene unit before appearance-item scale;
- image rows are converted to the Core frame convention in which +Y is up; and
- renderer adapters are responsible for any texture-coordinate convention differences in Qt/SDL/GPU APIs.

Because appearance items already have scale, no separate pixels-per-unit property is required initially. A later project/artwork-level density setting can be introduced if real use demonstrates a need.

### 6.6 RGBA convention

Core's canonical decoded image representation is RGBA8 with **straight/unpremultiplied alpha**.

Renderer adapters may convert to a premultiplied representation where their host API prefers it, but Core resource APIs and package decode semantics should remain stable and renderer-independent.

## 7. Naming, Rename, Deletion, and Topology-Lifetime Rules

### 7.1 Sprite-frame naming

Imported frame names are derived from source filenames without the extension.

Names must be unique within the character's atlas. On conflict, the editor should use a predictable auto-rename policy such as:

```text
head
head-2
head-3
```

The exact punctuation is UI policy; deterministic conflict handling is preferable to interrupting bulk folder import with repeated dialogs.

### 7.2 Renaming a sprite frame

Because the name **is** the resource identity, rename is a semantic operation.

Renaming:

```text
human_eye_closed -> human_eye_shut
```

must atomically update every appearance state mapping in that character that refers to `human_eye_closed`.

Animation events do not require changes because animations refer to slot/state, not concrete frame names.

The rename must be undoable as one operation.

### 7.3 Deleting a sprite frame

A valid character must not contain an appearance mapping to a nonexistent frame name.

Core should therefore reject raw deletion of a referenced frame. The editor may offer a compound user action such as:

```text
Delete "human_eye_closed" and remove 3 appearance mappings?
```

If confirmed, removal of the mappings plus the frame is one undoable command.

Explicit `none` is the representation for intentionally hidden states; dangling names are not.

### 7.4 Slot uniqueness and rename

A character's live rig may contain at most one bone declaring a particular non-empty slot.

Renaming a live slot is also a semantic operation. It must atomically update:

- the bone's `sprite_slot`;
- every appearance item in the character whose `slot` has the old value; and
- once sprite-state animation exists, every `sprite_state` event in that character's animations that uses the old slot.

This is exactly why ordinary bone display-name rename and slot rename are separate operations.

### 7.5 Semantic-state definition rename and deletion

State names are load-bearing character semantics just like slot names. Renaming a declared state, for example:

```text
half -> squint
```

must update every appearance-item mapping for that slot and, once sprite-state animation exists, every animation event that refers to the old `(slot, state)` pair.

Deleting a state should likewise be a semantic operation rather than erasing a string from a vector. Core/model code should reject deletion while references still exist or perform an explicit compound operation that removes/replaces those references. The mandatory `default` state cannot be deleted.

### 7.6 Deleting topology does not delete artwork bindings

Deleting a bone whose slot is referenced by appearances does **not** automatically delete those appearance items.

The character's artwork is intentionally more durable than any one topology component. The items become unresolved and are simply not rendered while no live bone supplies the slot.

The editor should visually indicate unresolved slots in the Artwork Browser/properties, but they remain valid character data.

This behavior also allows undo, rig restructuring, or later skeleton adoption to restore the slot without reconstructing its artwork configuration.

### 7.7 Character membership operations must consider slots

Once sprite slots exist, Make Character and Adopt Skeletons acquire an additional validation rule: the resulting rig's non-empty slot names must remain unique.

Adopting a skeleton whose slot name matches an **unresolved** appearance slot is desirable and should resolve the artwork. The only conflict is two live bones in the resulting rig claiming the same slot.

## 8. Draw Order and Layering

Every appearance item has explicit integer `draw_order`.

- lower values render first;
- higher values render later/in front;
- order is independent of item container order;
- order is independent of atlas packing/page order; and
- order is independent of skeleton guide rendering.

The editor should avoid ambiguous ties, for example by resequencing the items in one appearance to unique integer values after reordering actions.

User operations should include:

- Bring Forward;
- Send Backward;
- Bring to Front;
- Send to Back; and
- optional direct numeric editing.

The design deliberately allows several items with the same slot but different draw orders.

Character-to-character stage ordering is a separate question. The current project supports multiple characters and the character container must not accidentally determine rendering order through `unordered_map` iteration. Initial artwork implementation may render only according to an editor-defined deterministic character order, but a persistent stage/character Z-order should be designed separately if overlapping multiple characters becomes a required final-output feature.

## 9. Editor Artwork Browser and Authoring UX

### 9.1 Active character

The Artwork Browser is character-scoped.

The editor's current `selected_character()` concept is narrower than the browser needs because it succeeds only for direct character-frame selection. For artwork authoring, **active character** should resolve from semantic selection:

- selecting a character makes it active;
- selecting a skeleton, bone, or node that belongs to a character makes that parent character active; and
- selecting only loose topology clears the active character.

The initial implementation does not need a separately pinned Artwork Browser context, although that could be added later.

### 9.2 Browser presentation

The sprite library belongs to the character, not to the active appearance. Therefore switching appearances does not change which sprite frames exist in the browser.

A simple UI is:

```text
Character:  Alice
Appearance: [ Human v ] [New Appearance] [Appearance Actions...]

Sprite Library                                [Add Image...] [Add Folder...]
+-----------------------------------------------------------+
| [thumbnail] [thumbnail] [thumbnail] [thumbnail]           |
| head        eye-open   eye-half    eye-closed             |
|                                                           |
| [thumbnail] [thumbnail] ...                               |
| torso       upper-arm                                    |
|                                                           |
| Drop image files here                                     |
+-----------------------------------------------------------+
```

`QListView` icon mode or an equivalent thumbnail view is appropriate.

The user sees logical frame names and thumbnails. Packed atlas pages and rectangles are never an editor-level organizational concept.

### 9.3 Appearance creation

The shared character-level sprite library makes the old “Appearance from Folder” operation conceptually misleading: importing a folder adds frames to the character's shared atlas, not to the appearance that happened to be active when import occurred.

The initial commands should therefore be separate:

```text
New Appearance
Add Image...
Add Folder...
```

`New Appearance` creates only an appearance.

`Add Image...` / `Add Folder...` add frames to the active character's sprite library.

A future convenience command may create an appearance and then import a folder in one workflow, but it should be presented as convenience rather than imply resource ownership by the appearance.

### 9.4 Slot and semantic-state authoring

The slot/state schema belongs to the character's artwork, not to any one appearance, so the Artwork Browser should expose it independently of the active appearance.

A simple conceptual UI is:

```text
Slot: [ eyes v ]

States:
    default
    open
    half
    closed
    [+ Add State]
```

Assigning a new slot name to a bone creates/ensures the corresponding artwork slot definition with `default`. Selecting a slotted bone should make that slot the natural current slot in the Artwork Browser.

Switching Human -> Robot must **not** change the state list. The state vocabulary is shared by all appearances. What changes is the appearance's concrete mapping for those states.

For example:

```text
Slot: eyes
States: default, open, half, closed

Appearance: Human
    default -> human_eye_open
    open    -> human_eye_open
    half    -> human_eye_half
    closed  -> human_eye_closed

Appearance: Robot
    default -> robot_eye_open
    open    -> robot_eye_open
    half    -> (fallback to default)
    closed  -> robot_eye_shutter
```

The future animation editor uses the same slot definition to populate the valid state choices for a sprite-state event.

### 9.5 Import

Supported image files are decoded in Core and normalized to canonical RGBA8.

Initial import operations:

- Add Image... with multi-selection;
- Add Folder... without recursive traversal;
- drag one or more image files into the Sprite Library; and
- optional directory drag/drop later.

The source filesystem path may be retained as transient editor metadata for a future Reload from Source feature, but a saved project must not depend on the external path.

### 9.6 Binding by drag/drop

Dragging a sprite frame from the library onto a bone in the active character creates a new appearance item for that bone's slot.

If the bone does not yet have a slot, the editor should offer/create one. Exact default naming remains an editor-UX decision.

Dragging does **not** replace an existing item merely because another item already uses the slot. Multiple items per slot are valid.

The new item initially contains:

```text
slot       = target bone's sprite slot
states     = { "default" -> dragged frame name }
anchor     = chosen initial root/tip anchor
translation= (0,0)
rotation   = 0
scale      = (1,1)
draw_order = a deterministic front/back insertion value
```

The user may then add additional state mappings or additional layered items.

Cross-character drag/drop must not create a hidden reference to the source character's frame. The editor should either reject it or perform an explicit copy/import into the destination character before creating the item.

### 9.7 Sprite Transform tool

The Sprite Transform tool edits one appearance item while the rig is fixed reference geometry.

- dragging changes local translation;
- rotation handle changes local rotation;
- corner/edge handles change scale;
- negative scale is allowed;
- exact numeric T/R/S is available in properties;
- skeleton nodes/bones do not move;
- FABRIK/IK is not invoked; and
- only the selected item shows manipulation widgets.

The selection is tool-local rather than forcing appearance items into normal topology/model selection.

Frame registration origin is edited as a property of the sprite frame/resource, not by the ordinary binding transform tool. A later dedicated registration-origin editor may be added; initially numeric controls and/or a simple thumbnail-origin editor are sufficient.

## 10. View and Rendering Controls

Bone-driven artwork introduces separate character artwork and rig-guide layers.

```text
View
  Show Sprites [check]
  Show Skeleton [check]
  Skeleton Display >
    Normal
    Wireframe
```

Recommended editor behavior:

- Show Sprites on + Show Skeleton on: authoring mode;
- skeleton guide is normally drawn above sprites;
- Wireframe is useful while positioning artwork;
- Show Sprites on + Show Skeleton off: final-character preview; and
- item `draw_order` applies only within the appearance sprite layer.

The renderer must resolve one active appearance per rendered character instance and one current semantic sprite state per slot from animation evaluation.

## 11. Undo/Redo Model

Artwork operations should use the same command architecture as existing editor mutations.

Undoable operations include:

- create/delete/rename appearance;
- import/delete/rename sprite frame;
- edit frame registration origin;
- assign/rename/clear bone slot;
- add/rename/delete a slot semantic-state definition;
- add/delete/update appearance item;
- add/remove/change an appearance state mapping;
- root/tip anchor change;
- transform edit;
- draw-order edit; and
- future sprite-state animation edits.

Continuous mouse manipulation should create one logical undo step, not one command for every mouse-move event. The transform tool can edit transient state during drag and commit a before/after command at drag completion, following existing editor conventions where practical.

Image buffers are comparatively large. Command history should not repeatedly deep-copy immutable RGBA pixels for routine edits. Internal image resources may use shared immutable storage or move-owned snapshot objects even though semantic ownership remains character-local and no two characters share a mutable sprite resource.

Most importantly, character destruction undo must use the full character-payload snapshot described in section 2.3.

## 12. Clipboard Semantics

The current whole-character clipboard serializes topology/name-oriented data as JSON. Artwork should eventually participate in whole-character cut/copy/paste, but Phase 1 should not invent an ad-hoc second binary-image encoding merely for the clipboard before package resource serialization exists.

Artwork-aware whole-character clipboard support belongs with package/resource serialization in Phase 2.

When implemented:

- copying a whole character copies its complete artwork deeply;
- pasted characters do not share live image resources with the source character;
- sprite-frame names may be preserved because they are character-local;
- appearance-local IDs are regenerated or otherwise made independent as appropriate for duplicated character data;
- rig/topology IDs follow the existing duplication policy; and
- internal appearance mappings continue to refer to the copied frame names without remapping through project-global sprite IDs because no such IDs exist.

Loose topology copy/paste does not implicitly carry character artwork.

## 13. Project Package Format

### 13.1 Container

The current implementation already stores `.stickman` as a ZIP archive owned by Core with a `project.json` entry. Artwork extends this existing format.

```text
character.stickman   // physically a ZIP archive
```

### 13.2 Character-scoped package resources

Because artwork belongs directly to characters, image resource paths should be character-scoped:

```text
project.json
characters/
  <character-id-1>/
    artwork/
      page-0.png
      page-1.png
  <character-id-2>/
    artwork/
      page-0.png
```

There is no project-level atlas resource that several characters reference.

### 13.3 `project.json` is authoritative

`project.json` stores both semantic artwork data and the resource-location metadata Core needs to reconstruct the logical atlas.

A conceptual character entry is:

```json
{
  "id": "<character-id>",
  "name": "Alice",
  "skeletons": ["<skeleton-id>"],
  "artwork": {
    "slots": {
      "eyes": ["default", "open", "half", "closed"]
    },
    "frames": {
      "human_eye_open": {
        "origin": [0.0, 0.0],
        "page": 0,
        "rect": [10, 20, 60, 30]
      },
      "human_eye_closed": {
        "origin": [1.0, -2.0],
        "page": 0,
        "rect": [80, 20, 54, 12]
      }
    },
    "appearances": [
      {
        "id": "<appearance-id>",
        "name": "Human",
        "items": [
          {
            "slot": "eyes",
            "states": {
              "default": "human_eye_open",
              "open": "human_eye_open",
              "closed": "human_eye_closed"
            },
            "anchor": "root",
            "translation": [0.0, 0.0],
            "rotation": 0.0,
            "scale": [1.0, 1.0],
            "draw_order": 20
          }
        ]
      }
    ]
  }
}
```

Explicit hidden state may be encoded as JSON `null`:

```json
"hidden": null
```

The exact JSON shape may be adjusted to match current serialization style, but the responsibilities should remain as above.

### 13.4 No separate per-page atlas JSON is required

A sibling `page-N.json` file does not buy much when Core is the only canonical loader and `project.json` already names every logical sprite frame.

Page index/rectangle can be stored with each frame's package resource metadata in `project.json`. The physical page PNG then contains only pixels.

This removes an unnecessary consistency relationship among:

- project semantic data;
- page JSON; and
- page image.

Atlas placement remains non-semantic even though its current generated placement is recorded in `project.json`; repacking is free to change page/rectangle values on the next save.

### 13.5 Package reader/writer refactor

The current serializer is simple because the archive contains essentially one logical document. Artwork loading requires the archive to remain available while multiple images are read and decoded.

Before extending persistence, Core should introduce a small private RAII package reader/writer abstraction around `miniz` rather than spreading archive-open/extract/cleanup code through `sm_project.cpp`.

This helper remains private implementation detail and should preserve the current atomic-load property: all project/topology/character/artwork data is staged and validated before replacing the live project.

### 13.6 Packing ownership

Packing is a private Core save/resource operation.

When saving a character's artwork, Core:

1. obtains each logical frame's pixels;
2. packs frames into one or more atlas pages with `stb_rect_pack`;
3. generates page PNGs;
4. records page/rectangle resource metadata in `project.json`; and
5. writes all entries through the package writer.

Repacking must not change:

- character IDs;
- rig membership;
- appearance IDs;
- slot names and their declared state vocabularies;
- frame names;
- appearance mappings;
- registration origins;
- transforms; or
- draw order.

### 13.7 Packing rules

Initial packing should favor correctness and predictable sampling over maximum density:

- use `stb_rect_pack`;
- never rotate frames during packing;
- reserve at least a one-pixel padding border around every packed frame;
- **extrude the frame's edge texels into the padding** rather than filling the padding with unrelated transparent black;
- store only the true frame rectangle, excluding padding, in resource metadata;
- initialize unused page area as transparent RGBA8; and
- encode pages as alpha-preserving PNG.

Edge extrusion makes linear-filtered sampling safer at frame boundaries and avoids relying on renderer-specific treatment of transparent RGB values.

Maximum page size and multi-page policy remain implementation decisions. The logical atlas may always span multiple physical pages.

### 13.8 Vendored implementation dependencies

Core already vendors the JSON parser and `miniz`. Artwork resource support should add:

- `stb_image` for imported/package image decoding;
- `stb_image_write` for PNG atlas-page encoding; and
- `stb_rect_pack` for rectangle packing.

All remain private Core implementation dependencies.

## 14. Runtime Resource Resolution

A read-only runtime receives a character, selected appearance, evaluated animation state, and pose.

For each appearance item:

```text
item.slot
   |
   +--> resolve live bone with matching slot
   |
current animation slot state (or "default")
   |
   +--> state is validated against artwork slot definition
   |
   v
item.states[state]
   |
   +--> missing state: use item.states["default"]
   +--> explicit none: item is hidden
   |
   v
sprite-frame name
   |
   v
character.artwork.atlas[name]
   |
   v
Core returns frame/page image_view + source rectangle + frame origin
   |
   v
host uploads/uses texture and applies Core-defined transform semantics
```

Core supplies renderer-neutral bytes, geometry, and semantics. Host integrations create `QImage`/`QPixmap`, `SDL_Texture`, GPU textures, or engine-specific objects as appropriate.

A runtime should be able to cache textures per decoded atlas page while resolving logical frames by name.

## 15. Animation Integration

### 15.1 Character ownership

The character design already identifies animation as future character-level data. When animation ownership is implemented, animations should belong to the same durable `sm::character` that owns artwork, not to transient skeleton components.

This matters for sprite-state events because their semantic slot names belong to the character rig/artwork contract.

### 15.2 Extend the existing event variant

The current `sm::animation` timeline stores `animation_event` variants and presently supports skeletal rotation and translation. Sprite animation should add `sprite_state` to the same variant/concept:

```cpp
struct sprite_state {
    std::string slot;
    std::string state;
};

using animation_event = std::variant<rotation, translation, sprite_state>;
```

Exact type names are not important; the semantic rule is.

### 15.3 Evaluation rule

At animation time `t`, the current state of a slot is the state in the latest `sprite_state` event for that slot at or before `t`.

If there is none, the state is `default`.

There should not be two contradictory state events for the same slot at the same timestamp. Editor/model APIs should either replace the existing event or define one deterministic ordering policy; rejecting/replacing the duplicate is preferable to depending on vector insertion order.

### 15.4 Appearance independence

Animations never store concrete sprite-frame names.

This is the core invariant that allows one animation to work across Human, Robot, Skeleton, Summer, Winter, damaged, armored, and other appearances.

The animation's vocabulary is the **character artwork's declared slot/state schema**. The appearance's vocabulary is the concrete frame-name mapping. This is what allows the animation editor to offer validated state choices independent of the active appearance.

## 16. Phased Implementation Plan

### Phase 1 — Character-owned in-memory artwork and Sprite Library

Goal: establish character-local frame resources and appearance data in memory without binding them to the rig or persisting imagery yet.

Core:

- add `sm::artwork` as a direct `sm::character` member;
- at the same time, extend undo state for operations that actually destroy a character so undo preserves/restores its artwork while ordinary membership snapshots remain lightweight;
- add logical named `sm::sprite_atlas` / sprite-frame resources;
- add renderer-neutral RGBA8 image ownership/view APIs;
- add frame registration origin;
- add character-local appearances and appearance IDs;
- expose character-owned artwork as mutable state through the character while keeping rig/topology mutation project-controlled; let `sm::artwork` enforce artwork-local invariants through semantic operations;
- enforce unique frame names per character; and
- vendor/use `stb_image` for import decoding.

Editor:

- add character-scoped Artwork Browser;
- resolve active character from character/member selection;
- add New Appearance;
- add Add Image... and Add Folder...;
- add file drag/drop;
- show shared character Sprite Library thumbnails;
- implement frame rename/delete/origin editing; and
- maintain editor-only active appearance.

Explicitly out of scope:

- package persistence of artwork imagery;
- atlas-page packing;
- whole-character artwork clipboard;
- bone slots;
- appearance-item binding;
- sprite rendering on the rig;
- Sprite Transform tool; and
- sprite-state animation.

Acceptance criteria: independent characters can contain independent named frame libraries and appearances in memory; frame name conflicts and renames behave predictably; no artwork mutation leaks into another character.

### Phase 2 — Package persistence and artwork-aware whole-character clipboard

Goal: make character artwork durable and reuse the resource serialization machinery for complete character duplication.

- bump project format version as required;
- add private RAII package reader/writer abstraction;
- serialize each character's artwork semantic data in `project.json`;
- pack character frames into one or more pages with `stb_rect_pack`;
- use edge-extruded padding and no packed rotation;
- encode pages with `stb_image_write`;
- store frame page/rect metadata in `project.json`;
- deserialize/decode/validate all character resources atomically;
- keep loaded storage details hidden behind logical frame APIs;
- add whole-character clipboard deep-copy including artwork; and
- ensure pasted characters own independent artwork/resources.

Acceptance criteria: save/reopen reproduces character artwork exactly at the semantic level; page layout may change without changing names/mappings; whole-character copy/paste carries artwork without cross-character sharing.

### Phase 3 — Bone slots, appearance binding, rendering, and Sprite Transform

Goal: make static appearances drive visible character artwork.

Core/model:

- add string-valued `sprite_slot` to bones;
- enforce unique non-empty live slots within a character rig;
- add character-level artwork slot definitions with mandatory `default` plus user-defined semantic states;
- ensure assigning/adopting a slot creates/preserves its slot definition;
- update Make Character/Adopt Skeletons validation;
- add appearance items with slot, declared-state -> frame map, and one state-independent anchor/T/R/S/draw-order;
- allow multiple items per slot;
- add semantic slot rename with slot-definition/appearance-reference propagation;
- add semantic-state rename/delete operations that preserve appearance references and are ready to propagate to animation when Phase 4 lands;
- keep unresolved appearance items when topology removes their slot;
- add binding/resource validation; and
- serialize all slot/item data.

Editor/rendering:

- add bone slot editing;
- add Artwork Browser slot/state-schema editing (`default` plus user-defined states), shared across all appearances;
- drag frames onto bones to add appearance items;
- render the active appearance using `default` state;
- add root/tip controls;
- add Sprite Transform tool with tool-local item selection;
- add frame-origin editing support sufficient to register differing frame sizes;
- add draw-order controls; and
- add Show Sprites / Show Skeleton / Wireframe view controls.

Acceptance criteria: static cutout characters can be fully authored, layered, transformed, saved, reopened, and posed while artwork follows the correct rig slots and survives ordinary topology restructuring.

### Phase 4 — Sprite-state animation

Goal: allow animation timelines to switch semantic sprite states while remaining appearance-independent.

- make animation character-owned as required by the broader character animation design;
- add discrete `sprite_state {slot, state}` events to the existing animation event system;
- evaluate current slot state as the latest event at/before current time, defaulting to `default`;
- resolve state through the active appearance;
- support explicit hidden (`none`) mappings;
- fall back to the item's `default` mapping when a requested state is absent;
- update slot rename to rewrite sprite-state events;
- add timeline/editor controls whose state choices come from the character artwork slot definition rather than free-form strings; and
- optionally add authoring conveniences that generate repeated state sequences without changing the persisted semantic model.

Acceptance criteria: one animation containing sprite-state events can be played unchanged with several appearances, including appearances that reuse a frame for several states or omit a state and fall back to default.

## 17. Recommended Testing Strategy

### 17.1 Core ownership and lifetime

- every character owns exactly one artwork object;
- artwork is not present as a shared project-owned resource;
- deleting a character destroys its artwork;
- undoing actual character destruction restores its full artwork payload;
- routine topology split/merge undo does not deep-copy artwork merely because membership changed;
- rig split/merge leaves surviving character artwork untouched; and
- adopting topology never transfers artwork out of the character.

### 17.2 Names and symbolic resolution

- frame names are unique within one character atlas;
- two characters may use identical frame names independently;
- frame rename updates every appearance mapping atomically;
- slot names are unique among live bones in one character;
- every artwork slot definition contains `default` and survives temporary loss of its live bone;
- appearances cannot introduce state names outside the corresponding slot definition;
- slot rename updates its slot definition, all appearance items, and future sprite-state animation events;
- semantic-state rename updates appearance mappings and future sprite-state events;
- unresolved slot items remain valid after their bone is deleted; and
- later reintroduction/adoption of that slot resolves them again.

### 17.3 Appearance resolution

- `default` state resolution;
- animation/editor state choices come from the character-level slot definition;
- requested defined state resolution;
- requested missing state falls back to `default`;
- explicit `none` hides an item;
- multiple items on one slot resolve independently;
- two appearances map one slot/state to different frame names; and
- active-appearance switching preserves current semantic state.

### 17.4 Transform and registration

- known root-anchor transform case;
- known tip-anchor transform case;
- both anchors retain root-to-tip orientation;
- translation/rotation/non-uniform scale composition;
- changing semantic state does not change anchor/T/R/S/draw-order;
- negative-X scale mirrors correctly;
- differing-size frames with adjusted registration origin stay visually aligned; and
- image Y convention is correct in editor and a renderer-neutral test adapter.

### 17.5 Persistence/resource tests

- RGBA8 decode/encode round trip preserves alpha;
- logical frame names/origins survive package round trip;
- appearance mappings survive package round trip;
- atlas pages may repack without semantic changes;
- frames are never packed rotated;
- padding is present and edge-extruded;
- invalid page rectangles are rejected;
- missing page images are rejected;
- appearance reference to an unknown frame name is rejected;
- duplicate frame names in serialized character artwork are rejected;
- package loading succeeds without Qt/renderer dependencies; and
- failed artwork loading does not partially replace the live project.

### 17.6 Editor/manual scenarios

- select a bone belonging to a character and verify the browser scopes to its parent character;
- select loose topology and verify the browser clears/disables;
- bulk import frames with duplicate filenames;
- rename a referenced frame and verify appearance mappings survive;
- delete a referenced frame through the compound UI path;
- create a slot state vocabulary such as `eyes = {default, open, half, closed}` and verify it remains unchanged when switching appearances;
- create two layered items on one slot;
- change draw order around a joint seam;
- switch root/tip anchor;
- edit frame origin for differently sized eye frames;
- delete a slotted bone and verify artwork becomes unresolved rather than deleted;
- undo the deletion and verify immediate re-resolution;
- save/reopen;
- copy/paste a whole character after Phase 2 and verify independent imagery;
- play a blink state sequence against Human and Robot appearances after Phase 4; and
- switch appearances midway through that animation.

## 18. Open Decisions / Deferred Extensions

| Decision | Current recommendation / status |
|---|---|
| Sprite-frame identity | **Resolved:** unique load-bearing name within one character atlas; no opaque sprite ID. |
| Sprite-frame rename | **Resolved:** semantic rename; rewrite appearance mappings. |
| Appearance identity | Stable character-local ID is useful; it is not a project handle/object-index member. |
| Appearance-item identity | **Resolved for initial design:** no persistent opaque ID; item is an appearance-owned value and editor selection is transient/tool-local. |
| Slot identity | **Resolved:** semantic string declared by a bone; unique among live bones in one character. Character artwork retains the corresponding slot definition even when no live bone currently supplies it. |
| Slot state vocabulary | **Resolved:** explicit character-level artwork schema per slot, always containing `default`; appearances and animation use but do not define that vocabulary. |
| Slot rename | **Resolved:** propagate to artwork slot definition, appearance items, and future sprite-state events. |
| Semantic-state rename/delete | **Resolved conceptually:** semantic operation; update/remove appearance mappings and future animation references. `default` cannot be deleted. |
| Multiple items per slot | **Resolved:** allowed for layered artwork. |
| Multiple slots per bone | Deferred. Initial bone has zero/one slot. Can be generalized later if independent animated visual channels on one transform are needed. |
| Missing live bone for an appearance slot | **Resolved:** keep item as unresolved character data; do not delete it. |
| State mapping | **Resolved:** appearance item maps states declared by its character-level slot definition to frame names; every item has `default`. |
| Missing requested state | **Resolved:** fall back to the item's `default`. |
| Intentional hidden state | **Resolved:** explicit `none`/JSON `null`. |
| Sprite animation timing | **Resolved:** animation timeline owns timing; appearances own imagery. |
| Sprite animation event | **Resolved conceptually:** discrete `sprite_state(slot,state)` event that persists until changed. |
| Named sprite cycles | Deferred as editor convenience; can generate ordinary state events. |
| Frame registration | **Resolved:** resource-level origin, default image center. |
| Transform granularity | **Resolved initially:** anchor, T/R/S, and draw order are per appearance item and shared across all semantic states; state changes select only the frame. |
| Per-state transform deltas | Deferred; frame registration origin + common item transform should cover initial needs. Add an optional state-local delta only if real authoring requires it. |
| Pixel/world scale | **Resolved initially:** 1 source pixel = 1 Core unit before item scale. |
| Alpha convention | **Resolved:** Core canonical RGBA8 uses straight alpha. |
| Negative scale / mirroring | **Resolved:** allowed. |
| Node sprite binding | **Resolved:** not included initially; bone root/tip anchoring remains the model. |
| Root/tip orientation | **Resolved:** both use root -> tip axes; tip changes origin only. |
| Bone-length stretching | **Resolved initially:** none. |
| Draw order | **Resolved:** explicit per item, lower drawn first. |
| Character-to-character stage order | Deferred; do not derive from unordered character storage. |
| Appearance from Folder | **Removed as a core concept:** New Appearance and Import Folder are separate because frames belong to the character library, not an appearance. |
| Supported import formats | PNG required baseline; additional `stb_image` formats may be allowed by editor policy. |
| Source-file reload metadata | Optional transient/editor metadata; package never depends on source path. |
| Atlas page size | Open implementation choice. |
| Atlas page count | Logical atlas may span multiple pages. |
| Packed sprite rotation | **Resolved:** never rotate. |
| Atlas padding | **Resolved:** at least 1 pixel with edge extrusion. |
| Separate page JSON | **Resolved:** unnecessary; keep current page/rect resource metadata in `project.json`. |
| Active appearance persistence | Editor/runtime-instance state. Optional preferred/default appearance may be added later as character metadata. |
| Cross-character atlas sharing | **Resolved:** unsupported; reuse is explicit copy/import. |
| Whole-character artwork clipboard | Phase 2, after resource serialization exists. |
| Character payload undo | **Resolved implementation requirement:** when Phase 1 adds `artwork_`, operations that actually destroy a character must retain/restore its artwork; ordinary membership snapshots remain lightweight. |

## 19. Intended User Workflow

1. Create/open a `.stickman` project and build topology.
2. Create/select a character.
3. Open the Artwork Browser; it scopes itself to that character even when a member bone/node/skeleton is selected.
4. Import image files into the character's Sprite Library. Each receives a unique frame name.
5. Create an appearance such as Human.
6. Assign semantic slot names to the relevant bones; each new slot gets an artwork slot definition containing `default`.
7. In the Artwork Browser, add any additional valid states for a slot, for example `eyes = {default, open, half, closed}`. This vocabulary belongs to the character and is shared by every appearance.
8. Drag a frame onto a slotted bone to create an appearance item whose `default` state uses that frame.
9. Map additional declared states to alternate frame names as needed.
10. Add layered items to the same slot when needed for joint covers, highlights, etc.
11. Choose root/tip anchor and adjust the item's local T/R/S once; every semantic state of that item shares this placement.
12. Set draw order so overlapping pieces conceal seams correctly.
13. Adjust frame registration origins where alternate frames have different source dimensions/crops.
14. Create another appearance such as Robot. It uses the same slot/state schema but maps those states to different frame names.
15. Pose the character and verify both appearances follow the rig correctly.
16. Save through Core; sprite frames are packed into character-scoped atlas pages inside the `.stickman` archive.
17. When sprite animation is implemented, insert semantic state events such as `eyes=open/half/closed`; the editor offers only states declared for `eyes`.
18. Play the same animation with Human or Robot; animation timing/state remains unchanged while concrete imagery is resolved by the active appearance.

## 20. Design Summary

The central abstraction is intentionally simple:

```text
RIG
bone -> semantic slot name

ARTWORK SLOT SCHEMA
slot -> valid semantic states {default, ...}

ANIMATION
(slot, time) -> one declared semantic state

APPEARANCE
(slot, declared state) -> sprite-frame name
appearance item -> one shared anchor/T/R/S/draw-order across its states

ARTWORK ATLAS
sprite-frame name -> image + registration origin
```

Static artwork is the same model with `state = default`.

This keeps each layer responsible for one kind of meaning:

- topology/bones determine **where** artwork attaches;
- slot names determine **what semantic channel** that attachment represents;
- the artwork slot schema determines **which semantic visual states are valid for that channel**;
- animation determines **when that declared semantic visual state changes**;
- appearance determines **which concrete image represents that state and where that visual layer is placed**;
- the character atlas owns **the actual named image resources**; and
- the renderer evaluates the resulting transform and draws items in explicit order.

No sprite IDs, project-level artwork resources, appearance-owned atlases, or appearance-specific animation timelines are needed for the initial architecture.
