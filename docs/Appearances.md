# stick_man Artwork and Appearances

**Current implementation and design constraints**  
**Reviewed against the September 22, 2026 source selection**

## 1. Status

Artwork and appearances are implemented character data, not a future skinning plan.

The current system includes:

- character-local image/frame resources;
- per-frame registration origins;
- semantic slot definitions bound to bones;
- explicit root/tip bone anchors;
- per-slot semantic state vocabularies;
- multiple appearances;
- per-state frame mappings, including hidden mappings and fallback to default;
- per-appearance-slot translation/rotation/non-uniform scale;
- painter order represented by appearance-slot vector order;
- canvas artwork rendering and alpha-aware hit testing;
- canvas transform handles and numeric transform editing;
- editor-only active appearance and state preview;
- drag/drop of image frames onto rig bones;
- packaged-project persistence;
- Core sprite-page packing and PNG resource loading;
- animation preview against a detached evaluated topology;
- whole-character clipboard preservation of artwork, with bone-ID remapping on paste.

The terminology is deliberately **artwork**, **frame**, **slot**, **state**, and **appearance** rather than “skin.”

---

## 2. Ownership

`sm::character` owns one `sm::artwork` value directly.

Artwork is not a project-wide shared sprite library. Frames, slot definitions, and appearances are character-local.

Conceptually:

```text
character
    rig
    artwork
        frames
        slot definitions
        appearances
```

This makes a character a self-contained authored unit while still allowing all of its appearances to share the same underlying frame resources.

---

## 3. Core data model

The current semantic model is:

```cpp
struct sprite_frame {
    image_resource image;
    point registration_origin{};
};

enum class bone_anchor { root, tip };

struct slot_definition {
    object_id bone;
    bone_anchor anchor = bone_anchor::root;
    std::vector<std::string> states{"default"};
};

struct sprite_transform {
    point translation{};
    double rotation = 0;
    point scale{1, 1};
};

using frame_target = std::optional<std::string>;

struct appearance_slot {
    std::string slot;
    std::map<std::string, frame_target> states{{"default", std::nullopt}};
    sprite_transform transform;
};

struct appearance {
    std::vector<appearance_slot> appearance_slots;
};
```

`artwork` owns three named collections:

```text
frames       : map<string, sprite_frame>
slots        : map<string, slot_definition>
appearances  : map<string, appearance>
```

The map keys are semantic names local to the character artwork. Ordinary node/bone display names remain cosmetic and are not used to resolve artwork.

---

## 4. Frames

A frame is a logical image resource available to any appearance in the character.

A frame contains:

- immutable/copy-shareable decoded image data through `image_resource`;
- a registration origin in the frame's centered Cartesian image coordinate system.

The frame name is its artwork-local semantic key.

Current operations include:

- insert/decode a frame;
- rename a frame;
- delete an unreferenced frame;
- edit registration origin.

Renaming a frame propagates through appearance mappings. Deleting a frame is rejected while an appearance still references it.

Frame dimensions are bounded so Core can reserve packing padding within the image-resource maximum size.

---

## 5. Slot definitions

A slot is the stable semantic interface between a character rig and its interchangeable appearance artwork.

Examples might be:

```text
head
upper_arm_l
hand_r
eye_l
mouth
```

A slot definition stores:

```text
bone ID
anchor = root | tip
semantic state vocabulary
```

The slot name, rather than the bone's display name, is the semantic key used by appearances.

### 5.1 Root and tip anchoring

The anchor chooses which endpoint supplies the local origin:

```text
root -> bone parent/root node
 tip -> bone child/tip node
```

Both anchors use the same root-to-tip bone orientation. Choosing `tip` moves the origin; it does not reverse the local axes.

Node-specific sprite bindings are therefore unnecessary in the current model. Artwork centered at a joint can bind to an incident bone and choose the endpoint coincident with that joint.

### 5.2 Semantic states

Each slot defines its own state vocabulary. Every slot always includes the state:

```text
default
```

Additional examples might be:

```text
eye: default, blink, closed
mouth: default, smile, frown, open
hand: default, fist, point
```

State vocabulary belongs to the slot definition so every appearance agrees on the semantic meaning even when it maps that state to different image frames.

The `default` state cannot be renamed or deleted.

Renaming/deleting a non-default state propagates through appearance mappings for that slot.

---

## 6. Appearances

An appearance is an ordered list of implementations of semantic slots.

Each `appearance_slot` stores:

- the semantic slot name;
- a state-to-frame mapping;
- one local sprite transform shared by the slot's state frames.

An appearance does not have to implement every slot.

### 6.1 State mappings

Each included slot must explicitly contain a `default` mapping.

A mapping value is `optional<string>`:

- a frame name means draw that frame;
- `nullopt` means intentionally hidden.

For non-default states, a missing map entry means **inherit the default mapping**.

Thus there are three useful states for an appearance mapping:

```text
explicit frame
explicitly hidden
inherit default
```

This is different from a missing slot implementation: if the appearance omits the slot itself, nothing is drawn for that slot.

### 6.2 Local transform

The appearance slot stores:

```text
translation
rotation (radians in Core/persistence)
scale X/Y
```

The transform is appearance-specific and slot-specific but currently not state-specific. Switching a slot from one frame state to another keeps the same local placement transform.

### 6.3 Painter order

Painter order is the order of `appearance.appearance_slots`.

Earlier entries draw first; later entries draw on top.

There is no separate persisted integer `draw_order` in the current model. Reordering the vector is the semantic layering operation.

This supersedes the older design proposal that used an explicit numeric draw-order property.

---

## 7. Transform semantics

Artwork resolution produces a world transform for each visible slot.

For a resolved bone:

```text
anchor position = root or tip node world position
bone orientation = current root -> tip world rotation
```

Core composes:

```text
bone_transform
    = translate(anchor position)
    * rotate(current bone angle)

sprite_transform
    = bone_transform
    * translate(appearance-slot translation)
    * rotate(appearance-slot rotation)
    * scale(appearance-slot X/Y scale)
    * translate(-frame registration origin)
```

Bone length does not implicitly stretch a sprite. Scale is explicit appearance data.

The resolved result includes both the final transform and the pre-local `bone_transform`, which allows the editor's transform handles to edit local placement correctly.

---

## 8. Rendering and animation preview

`project::resolve_artwork()` accepts an optional geometry topology.

Normally it resolves the slot's bone in the persistent project topology. During Animation Mode the canvas supplies the detached evaluated working topology instead.

This is important: the persistent slot still names the same bone ID, but its current position/orientation can come from the animation preview topology.

Conceptually:

```text
persistent artwork semantics
    slot -> bone ID
    appearance/state mapping
    local transform
        +
current geometry topology
        ->
resolved sprites
```

Therefore artwork already follows Rigid/IK rotation and translation during animation playback and scrubbing. No duplicate animation-specific artwork representation is required for skeletal motion.

The editor owns active appearance and preview-state selection. These are session/presentation state, not fields stored on the character as “currently active appearance/state.”

---

## 9. Artwork Browser

The current Artwork Browser has three authoring concerns.

### 9.1 Structure

The Structure view manages semantic slot definitions and their state vocabularies.

The user can:

- create/delete/rename slots;
- bind a slot to a bone;
- choose root or tip anchor;
- add/rename/delete semantic states.

### 9.2 Appearances

The Appearances view manages appearance definitions and the ordered slot implementations.

The user can:

- create/rename/delete appearances;
- include/exclude semantic slots in an appearance;
- choose frame/hidden/inherited mappings per state;
- preview a semantic state;
- reorder appearance slots to change painter order;
- edit local translation, rotation, and X/Y scale numerically;
- reset a slot transform.

### 9.3 Images

The Images view exposes the character-local frame resources. Frames can be imported/managed and dragged onto the canvas/bones as part of the binding workflow.

The editor displays logical frames, not packed sprite-page rectangles.

---

## 10. Canvas editing

The canvas artwork layer supports:

- drawing visible sprites in appearance painter order;
- stable cross-character drawing order;
- alpha-aware sprite hit testing;
- selected-sprite outline;
- direct translate/rotate/scale handles when transform editing is enabled;
- local transform preview while dragging;
- frame drag/drop targeting bones;
- active appearance selection;
- per-slot semantic state preview.

The rig remains the reference geometry for artwork transforms. Editing a sprite's appearance transform does not invoke IK or change skeleton node positions.

---

## 11. Topology changes and unresolved slots

Artwork handles destructive topology changes differently from animation actions.

Animation actions with deleted persistent dependencies are removed because keeping an action that cannot evaluate would violate the ordinary-editor referential-integrity invariant.

Artwork slot definitions, however, may remain with a bone ID that no longer resolves to the character. `project::slot_resolved()` checks both:

- that the ID resolves to a bone; and
- that the bone belongs to the character that owns the artwork.

Unresolved slots are skipped during rendering.

This allows artwork definitions to survive some rig editing without silently rebinding themselves to a different bone.

When topology replacement deliberately assigns a fresh ID to a surviving bone, the project's replacement machinery remaps artwork bone IDs so the authored binding follows that surviving semantic bone.

This distinction is intentional current behavior and should be preserved/documented if a future “repair unresolved artwork slot” workflow is added.

---

## 12. Validation rules

`sm::artwork` enforces local semantic consistency for normal mutation operations:

- names cannot be empty;
- names are unique within their corresponding artwork map;
- frame dimensions and registration origins must be valid;
- slot anchors must be root or tip;
- slot state names are unique and include `default`;
- an appearance cannot contain the same slot twice;
- every appearance slot must name an existing slot definition;
- every appearance slot must contain a default mapping;
- mapped states must be declared by the slot;
- mapped frame names must exist;
- local transforms must contain finite values.

Bone existence/ownership is checked at the project resolution boundary rather than by standalone `artwork` mutation. This is why unresolved slot definitions can persist safely.

---

## 13. Persistence and sprite-page packing

Artwork is stored inside the Core-owned `.stickman` packaged project.

Project format version 6 writes character artwork metadata into `project.json` and character-specific PNG sprite pages into the package.

The current resource model does **not** use a separate atlas JSON file per page. Page names and frame rectangles are represented directly by the artwork JSON in the semantic project document.

For each character artwork package, serialized data contains:

```text
pages
frames
    name
    registration origin
    page index
    source rectangle
slots
    name
    bone ID
    root/tip anchor
    state vocabulary
appearances
    name
    ordered slots
    state mappings
    translation/rotation/scale
```

### 13.1 Packing

Packing is a private Core persistence concern.

Current behavior:

- `stb_rect_pack` places rectangles;
- the default page size is at least 2048 and grows to fit the largest padded frame;
- multiple pages are emitted when required;
- sprites are not rotated by packing;
- padding defaults to one pixel;
- padding pixels are **edge-extruded** from the image, rather than left transparent;
- stored frame rectangles exclude the padding;
- pages are encoded as PNG;
- packing layout is not semantic identity.

Repacking may change page placement without changing frame names, slot mappings, or appearance semantics.

### 13.2 Loading

Core reads PNG pages, validates page naming/rectangles, creates image regions, then reconstructs frames/slots/appearances through the same semantic APIs.

Qt/SDL/GPU objects do not appear in Core artwork data.

---

## 14. Clipboard behavior

Whole-character copy/cut/paste currently preserves artwork.

The clipboard payload contains topology separately and embeds a serialized temporary Core package for character resources. On paste:

- the topology receives fresh skeleton/node/bone IDs;
- artwork is recovered from the resource package;
- artwork bone bindings are remapped to the new bone IDs;
- the character root bone is remapped similarly.

At present the same character-copy path does **not** preserve animation assets. That is a character/animation gap rather than an artwork gap and should be fixed before the action vocabulary grows substantially.

---

## 15. Semantic states and animation

Semantic states are currently authoring/preview data, not animation actions.

The artwork layer can preview a chosen state for a slot, but no current animation action changes:

- a slot's semantic state;
- the active appearance;
- an appearance-slot transform;
- a frame registration origin.

These are plausible future animation capabilities, especially semantic state changes such as blink/mouth/hand state. They should not be added until the animation action-extension/remapping/integrity work described in `Animation.md` is in place.

When state animation is designed, prefer animating the semantic state (`blink`) rather than directly naming a frame (`eyes_closed.png`) so the same animation can work across different appearances.

---

## 16. Remaining artwork/appearance work

The core artwork system is usable and internally coherent. Remaining work is mainly integration/polish rather than another data-model rewrite.

Important follow-ups are:

1. **Define repair UX for unresolved slot bindings.** Current rendering safely skips them, but the editor should make the unresolved condition obvious and make rebinding easy.
2. **Decide whether project-level validation should optionally enforce fully resolved artwork.** Today unresolved slot references are tolerated intentionally.
3. **Complete character-level copy semantics.** Artwork already remaps correctly; animation should join it so a copied character is genuinely self-contained.
4. **Keep preview state separate from authored animation state.** If semantic-state actions are added, do not accidentally serialize the editor's current preview selection as character state.
5. **Add focused persistence/transform tests** for packing, edge padding, state fallback, root/tip anchors, painter order, remapping, and animation-preview geometry.

---

## 17. Stable design principles

Future artwork work should preserve these rules:

- artwork belongs to a character;
- frames are logical named resources independent of packing layout;
- slots are semantic rig interfaces independent of bone display names;
- appearances map semantics to artwork rather than redefining rig structure;
- root/tip changes the anchor origin, not the root-to-tip orientation;
- painter order is explicit authored appearance order;
- active appearance and preview states are editor/session choices unless an explicit animation/runtime feature says otherwise;
- Core owns renderer-neutral resource semantics and package persistence;
- Qt owns editor presentation and interactive handles;
- animation preview resolves persistent artwork against evaluated rig geometry rather than duplicating artwork into the animation working topology.
