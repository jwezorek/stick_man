# stick_man Artwork and Appearances

Design for character-owned bitmap artwork, appearance slots, semantic sprite states, editor authoring, package persistence, and runtime resource access.

Review draft — September 13, 2026

## 1. Overview

Each `sm::character` owns its artwork directly. Artwork contains:

- a character-local library of named sprite frames;
- a character-local set of slot definitions;
- zero or more appearances.

Conceptually:

```text
character
├── rig
└── artwork
    ├── frames
    ├── slot definitions
    └── appearances
        └── ordered appearance slots
```

The central model is:

```text
slot definition
    -> which bone drives this visual channel
    -> root/tip anchor
    -> which semantic states are valid

appearance slot
    -> which slot definition it implements
    -> which frame represents each semantic state
    -> local translation / rotation / scale

appearance
    -> ordered appearance slots
    -> order is painter order

sprite frame
    -> named image + registration origin
```

For a static body part, the slot has only the mandatory `default` state. For an animated visual feature such as eyes, the slot may define states such as `open`, `half`, and `closed`.

An animation eventually changes semantic slot state, not concrete frame names:

```text
eyes = closed
```

Each appearance decides which concrete sprite frame represents that state.

The in-memory model deals only in logical individual sprite frames. Packed sprite sheets are a Core persistence/runtime resource representation. They are generated when needed and are not part of editor-facing artwork semantics.

## 2. Identity and Namespaces

### 2.1 Project object IDs

`sm::object_id` remains the identity mechanism for project/model objects such as characters, skeletons, nodes, and bones.

Artwork uses those IDs only where it needs to refer back into the project model. In particular, a slot definition stores the `object_id` of the bone that drives it.

Artwork-internal resources do not need project-level object IDs.

### 2.2 Character-local string identities

The following are character-local symbolic names:

- sprite-frame names;
- slot names;
- semantic state names; and
- appearance names.

They are unique within the scope that owns them and may be used directly as keys.

Examples:

```text
frame:      "eye_open"
slot:       "eyes"
state:      "closed"
appearance: "Robot"
```

Two different characters may use the same names without conflict.

A rename is a semantic operation within one character's artwork and must update references in that artwork. Slot/state renames must also update future animation references when animation integration exists.

## 3. Sprite Frames

A sprite frame is one concrete bitmap image available to a character's appearances.

Conceptually:

```cpp
struct sprite_frame {
    image_resource image;
    point registration_origin{};
};

using frame_library = map<string, sprite_frame>;
```

The exact image-storage type is an implementation detail. Core owns renderer-neutral image data; Qt, SDL, and GPU texture types remain outside Core.

### 3.1 Frame names

Frame names are unique within one character's frame library.

Import should derive the initial frame name from the source filename without its extension and resolve conflicts predictably, for example:

```text
eye.png      -> eye
eye.png      -> eye_2
eye.png      -> eye_3
```

The exact suffix policy is editor behavior, not a persistence requirement.

### 3.2 Registration origin

Every frame has a registration origin used to align frames of different dimensions or crops.

The default registration point is the geometric image center.

Registration coordinates are expressed in a frame-local Cartesian coordinate system:

```text
+X right
+Y up
```

The pixel buffer's physical row order is not part of this semantic convention. Core image access and renderer adapters handle conversion between image-memory coordinates and frame-local coordinates.

A frame change therefore does not have to move a feature merely because the replacement bitmap has different dimensions.

Example:

```text
eye_open      80 x 30   origin = (0, 0)
eye_closed    72 x 12   origin = (1, -3)
```

The registration origin belongs to the frame resource and applies everywhere that frame is used.

### 3.3 Pixel format

Core's canonical decoded image format is RGBA8 with straight/unpremultiplied alpha.

The initial scale convention is:

```text
1 source-image pixel = 1 Core scene unit before appearance-slot scaling
```

Renderer adapters may convert the pixels to whatever representation their host API prefers.

### 3.4 Logical frames and physical image backing

A sprite frame is a logical image resource. Its physical backing is not part of its semantic identity and may change during the lifetime of a project.

Core must support frames backed by either:

- standalone RGBA8 image data, such as a bitmap newly imported by the editor; or
- a rectangular region of a decoded packed sprite sheet, such as a frame reconstructed when loading a `.stickman` package.

Both forms are exposed uniformly through the frame/resource API. Code using artwork should not need to know whether a named frame owns standalone pixels or refers to a region of shared sheet storage.

A live character may therefore contain both kinds of backing at the same time. For example, frames loaded from the package may still refer to decoded sheet regions while newly imported frames are backed by standalone images. This mixed representation is normal and must not require immediate repacking.

The exact storage mechanism is private to Core. An implementation might use shared sheet buffers plus image cells, standalone image buffers, variants, or another representation. The semantic model only requires that every frame can be accessed as a logical image with its dimensions, pixels, and registration origin.

## 4. Slot Definitions

A slot definition is a stable character-artwork visual channel. It connects artwork semantics to one live bone in the character's rig.

Conceptually:

```cpp
enum class bone_anchor {
    root,
    tip
};

struct slot_definition {
    std::string name;
    object_id bone;
    bone_anchor anchor = bone_anchor::root;
    std::vector<std::string> states;
};
```

Every slot definition has:

- a character-local unique name;
- a bone `object_id`;
- an explicit root/tip anchor; and
- a semantic state vocabulary.

A character may define several slots driven by the same bone. This is important because one bone may drive several independently layered or independently stateful pieces of artwork.

For example:

```text
bone: head
    slot "face"
    slot "eyes"
    slot "glasses"

bone: torso
    slot "body"
    slot "shirt"
```

Slots are artwork data. `sm::bone` does not need a sprite-slot field.

### 4.1 Root/tip anchor

The anchor selects which endpoint of the bone supplies the slot's origin.

```text
root anchor -> current root-node position
tip anchor  -> current tip-node position
```

Both anchors use the same local axes:

```text
+X = root -> tip
```

Choosing `tip` moves the origin to the tip but does not reverse the local frame.

The anchor belongs to the slot definition because it describes how this visual channel follows the rig, independent of which appearance is active.

### 4.2 Slot lifetime and unresolved bones

A slot definition references a bone by stable `object_id`.

If ordinary topology editing temporarily removes that bone, the artwork definition should remain rather than silently deleting authored artwork. The slot becomes unresolved until the referenced bone is restored or the user explicitly rebinds/deletes the slot.

This is particularly useful for undo/redo and for structural editing that temporarily changes topology.

Core should be able to distinguish:

```text
resolved slot   -> referenced bone exists and belongs to this character
unresolved slot -> referenced bone is absent or no longer belongs to this character
```

Rendering skips unresolved slots. The editor should visibly identify them as needing repair.

### 4.3 Semantic state vocabulary

Every slot definition contains the mandatory state:

```text
default
```

Static slots need no other states.

Stateful slots may add user-defined values:

```text
eyes = { default, open, half, closed }
mouth = { default, smile, open }
hand = { default, open, fist, point }
```

State names are unique within one slot definition.

The slot owns this vocabulary so every appearance agrees on the semantic states available for that visual channel.

The appearance maps those semantic states to concrete images; it does not invent new state names.

## 5. Appearances

An appearance is one visual realization of the character: for example Human, Robot, Summer, Winter, Armored, or Skeleton.

Conceptually:

```cpp
struct appearance {
    std::string name;
    std::vector<appearance_slot> slots;
};
```

Appearance names are unique within the character's artwork.

An appearance does not own its own frame library. All appearances of one character select from the character's shared frame library.

### 5.1 Appearance slots

An appearance slot implements one character slot definition for one appearance.

Conceptually:

```cpp
struct sprite_transform {
    point translation{};
    double rotation = 0.0; // radians
    point scale{1.0, 1.0};
};

using frame_target = optional<string>; // nullopt means hidden

struct appearance_slot {
    std::string slot;
    std::map<std::string, frame_target> states;
    sprite_transform transform;
};
```

An appearance slot contains:

- the name of the slot definition it implements;
- a mapping from semantic state names to concrete frame names or explicit hidden state; and
- the local translation/rotation/scale used by that appearance.

At most one appearance slot should implement a given slot definition within one appearance. If two visual layers are needed on the same bone, create two slot definitions. That keeps slot identity equal to one independently drawable visual channel.

An appearance may omit a slot definition. During authoring this allows incomplete appearances; at runtime an omitted slot is simply not drawn.

### 5.2 Painter order

The order of `appearance.slots` is the draw order.

```text
appearance.slots[0]       drawn first  -> back
appearance.slots[1]
...
appearance.slots[n - 1]   drawn last   -> front
```

Rendering therefore uses the painter's algorithm.

There is no separate integer `draw_order`, no tie behavior, and no resequencing invariant.

Editor operations are ordinary vector/list reordering:

- Bring Forward;
- Send Backward;
- Bring to Front;
- Send to Back; and
- drag to reorder in the appearance slot list.

Different appearances may use different slot ordering when their artwork requires different layering.

### 5.3 State-to-frame mapping

Every appearance slot must define its `default` mapping.

A mapping target is either:

```text
frame name
```

or:

```text
none
```

where `none` means the slot is intentionally hidden in that state.

Example:

```text
slot definition:
    eyes = { default, open, half, closed }

Human appearance slot:
    default -> human_eye_open
    open    -> human_eye_open
    half    -> human_eye_half
    closed  -> human_eye_closed

Robot appearance slot:
    default -> robot_visor_on
    open    -> robot_visor_on
    closed  -> robot_visor_dark
```

A non-default state mapping may be omitted. When the current state has no explicit mapping, rendering falls back to `default`.

This allows an appearance to distinguish only the states that matter to it.

An explicit `none` is different from an omitted mapping:

```text
missing mapping -> fall back to default
explicit none   -> draw nothing
```

### 5.4 Appearance-slot transform

The slot definition determines which bone and endpoint drive the visual channel. The appearance slot determines how this appearance's imagery is positioned relative to that channel.

Translation, rotation, and scale are shared across all semantic states of that appearance slot. Changing `eyes` from `open` to `closed` changes the selected frame, not its authored transform.

Frame registration origins handle the usual case where alternate state images have different dimensions/crops.

A later per-state transform delta may be added if real authoring experience requires it, but it is not part of the initial model.

## 6. Core Data Model

A compact conceptual model is:

```cpp
struct sprite_frame {
    image_resource image;
    point registration_origin{};
};

enum class bone_anchor {
    root,
    tip
};

struct slot_definition {
    std::string name;
    object_id bone;
    bone_anchor anchor = bone_anchor::root;

    // Always contains "default".
    std::vector<std::string> states{"default"};
};

struct sprite_transform {
    point translation{};
    double rotation = 0.0;
    point scale{1.0, 1.0};
};

using frame_target = std::optional<std::string>;

struct appearance_slot {
    std::string slot;

    // Must contain "default". Other keys must be declared by
    // the corresponding slot_definition.
    std::map<std::string, frame_target> states;

    sprite_transform transform;
};

struct appearance {
    std::string name;

    // Painter order: earlier slots draw behind later slots.
    std::vector<appearance_slot> slots;
};

struct artwork {
    std::map<std::string, sprite_frame> frames;
    std::map<std::string, slot_definition> slot_definitions;
    std::vector<appearance> appearances;
};
```

The exact containers may differ in implementation. In particular, editor ordering and lookup performance may justify maintaining both an ordered vector and an internal name index.

`sm::character` owns one `sm::artwork` directly:

```cpp
class character {
    const object_id id_;
    std::string name_;
    std::reference_wrapper<project> owner_;
    sm::rig rig_;
    sm::artwork artwork_;
};
```

### 6.1 Core invariants

Core should enforce at least the following:

1. Frame names are unique within one artwork.
2. Appearance names are unique within one artwork.
3. Slot-definition names are unique within one artwork.
4. Every slot definition contains `default` exactly once.
5. State names are unique within their slot definition.
6. Every appearance slot references a known slot definition.
7. An appearance contains at most one appearance slot for a given slot definition.
8. Every appearance-slot state key is declared by that slot definition.
9. Every non-null frame target names a frame in the same artwork.
10. Every appearance slot has a `default` mapping.
11. A resolved slot's bone belongs to the owning character's rig.

Artwork-local validation should live in `sm::artwork` operations rather than being reproduced by editor code.

### 6.2 Mutable access

The current project API deliberately restricts generic mutable lookup to nodes and bones. Artwork does not require reopening mutable `sm::character` through `project::get()`.

A narrow API is preferable, for example conceptually:

```cpp
artwork& project::artwork(const object_id& character_id);
const artwork& project::artwork(const object_id& character_id) const;
```

or equivalent semantic project operations.

Rig/topology mutation remains project-controlled.

## 7. Rendering and Resolution

Rendering one character requires:

- the character's current rig pose;
- one selected appearance; and
- the current semantic state for each stateful slot.

For each appearance slot, in vector order:

```text
appearance_slot.slot
        |
        v
slot definition
        |
        +--> referenced bone object_id
        +--> root/tip anchor
        +--> valid state vocabulary
        |
        v
current semantic state for slot
        |
        v
appearance_slot.states[state]
        |
        +--> missing non-default mapping -> default mapping
        +--> explicit none -> skip draw
        |
        v
sprite-frame name
        |
        v
character artwork frame library
        |
        v
frame image + registration origin
        |
        v
compose bone frame + appearance-slot transform
        |
        v
draw
```

### 7.1 Default state

If no animation or runtime state has selected another value for a slot, its state is `default`.

A completely static character therefore uses exactly the same machinery as a stateful character.

### 7.2 Bone-local transform

The canonical bone-local orientation is:

```text
+X = current bone root -> current bone tip
```

The slot's selected root/tip endpoint supplies the world-space origin. Both anchor choices keep the same +X direction.

Conceptually:

```text
slot_world =
    translate(current_anchor_position)
    * rotate(current_bone_angle)

sprite_world =
    slot_world
    * translate(appearance_slot.translation)
    * rotate(appearance_slot.rotation)
    * scale(appearance_slot.scale)
    * registration_adjustment(frame.registration_origin)
```

The precise matrix convention should follow existing Core math conventions, but these semantics must remain stable.

### 7.3 Bone length

Bone length does not automatically scale the sprite.

A root-anchored slot remains at the root as bone length changes. A tip-anchored slot follows the tip. Neither is implicitly stretched.

### 7.4 Mirroring

Negative X or Y scale is valid and may be used for mirroring where useful.

## 8. Editor Model and UX

### 8.1 Active character and appearance

The Artwork Browser operates on one character at a time.

When the user selects a character, or a node/bone/skeleton belonging to a character, the editor can resolve that character as the active artwork context.

Loose topology has no character artwork context, so artwork controls should clear or disable appropriately.

The active appearance is editor/session state. It is not part of skeleton state and need not initially be persisted in the project.

### 8.2 Artwork Browser

A practical browser can expose three related things:

```text
Character: Alice
Appearance: [ Human v ]

Appearance slots / draw stack
--------------------------------
[front]  glasses
         eyes
         face
         shirt
         body
[back]   rear_arm

Sprite frames
--------------------------------
[thumb] head
[thumb] eye_open
[thumb] eye_closed
[thumb] torso
...
```

The exact pane arrangement is an editor implementation choice. The important distinction is that:

- slot definitions describe character visual channels;
- appearance slots describe the active appearance's concrete implementation and painter order; and
- sprite frames are reusable character-local image resources.

### 8.3 Creating slot definitions

The editor should support creating a slot from a selected bone.

A new slot requires:

- a unique name;
- the selected bone;
- root or tip anchor; and
- initial state vocabulary `{ default }`.

A sensible default name may be derived from the bone display name or from the first assigned frame, but the user can rename it.

Several slots may be created on the same bone.

### 8.4 Adding a slot to an appearance

An appearance can add an implementation for any slot definition it does not already implement.

The new appearance slot should begin with:

```text
default -> none
translation = (0, 0)
rotation = 0
scale = (1, 1)
```

or, when created by dropping a frame, with `default` mapped directly to that frame.

Its position in the appearance's slot list immediately determines painter order.

### 8.5 Dragging a frame onto a bone

Dragging a frame from the browser onto a bone should be a convenient compound authoring action.

If the active appearance already has one or more slots driven by that bone, the editor may let the user choose one.

Otherwise it may create:

1. a new slot definition bound to that bone;
2. an appearance slot implementing it; and
3. a `default` mapping to the dropped frame.

The exact quick-create naming policy is editor behavior and can be refined during implementation.

### 8.6 Semantic states

Slot state editing belongs with the slot definition.

For an `eyes` slot, the UI might show:

```text
States
    default
    open
    half
    closed
```

The active appearance then shows its mappings:

```text
default  -> eye_open
open     -> eye_open
half     -> eye_half
closed   -> eye_closed
```

Adding/removing/renaming a semantic state affects the slot definition and all appearance mappings for that slot.

`default` cannot be removed or renamed.

### 8.7 Painter-order editing

Because appearance-slot order is draw order, the UI should make reordering direct and obvious.

Useful actions:

- drag slots in the list;
- Bring Forward;
- Send Backward;
- Bring to Front;
- Send to Back.

No numeric draw-order property is required.

### 8.8 Sprite Transform tool

The Sprite Transform tool edits the selected appearance slot while treating skeleton geometry as fixed reference data.

It should support:

- translation;
- rotation;
- X/Y scale;
- exact numeric values in the properties UI; and
- selection of the slot's rendered sprite on the canvas.

The tool must not move nodes, change bones, or invoke IK.

The selected artwork item is editor/tool state, not a project model handle. There is no need to extend `mdl::handle` or generic project selection merely to manipulate an appearance slot.

### 8.9 Frame registration editing

The frame browser/properties UI should allow editing a frame's registration origin.

This is especially useful for state sequences whose source images were exported with different bounding boxes.

## 9. Rename and Delete Semantics

Artwork names are referenced inside a contained character-owned aggregate, so rename/delete operations should be implemented as semantic artwork operations.

### 9.1 Frame rename

Renaming a frame updates every appearance-state mapping that references it.

### 9.2 Frame delete

Deleting an unreferenced frame is immediate.

Deleting a referenced frame should be an explicit compound editor operation. The initial UI may either:

- reject deletion and show its references; or
- confirm deletion and rewrite affected targets to `none`.

Core should not leave dangling frame-name references.

### 9.3 Slot rename

Renaming a slot updates every appearance slot that references it and, once animation integration exists, every semantic state event that references it.

### 9.4 Slot delete

Deleting a slot definition removes its corresponding appearance slots from every appearance.

Once animation integration exists, the editor must also remove or reject any animation events that reference that slot.

### 9.5 State rename/delete

Renaming a non-default state rewrites its appearance mappings and future animation events.

Deleting a non-default state removes its appearance mappings and future animation events.

`default` is mandatory and cannot be deleted or renamed.

### 9.6 Bone deletion and rebinding

Deleting the bone referenced by a slot does not implicitly delete the slot or its appearance data.

The slot becomes unresolved. Undoing the topology edit naturally resolves it again because the same bone ID is restored.

The editor should offer an explicit rebind operation to point an unresolved slot at another bone if desired.

## 10. Undo, Redo, and Clipboard

All user-visible artwork mutations should participate in the existing editor undo/redo model.

Useful command granularity includes:

- import/delete/rename frame;
- edit frame registration origin;
- create/delete/rename slot definition;
- bind/rebind slot to bone;
- edit root/tip anchor;
- add/rename/delete semantic state;
- create/delete/rename appearance;
- add/remove/reorder appearance slot;
- edit state-to-frame mapping; and
- edit appearance-slot transform.

Continuous transform dragging should coalesce into one undoable edit where practical.

Whole-character duplication/copy should deep-copy artwork along with the character. Frame names, slot names, appearance names, and state names can be preserved exactly because the copied character owns an independent namespace. Bone references inside slot definitions must be remapped to the copied bones' new project IDs.

Copying loose topology does not carry character artwork unless the editor explicitly performs a whole-character copy operation.

## 11. Project Package Persistence

The current `.stickman` format is already a ZIP archive owned by Core with `project.json` as its semantic document. Artwork extends that format.

### 11.1 Canonical in-memory representation

The canonical in-memory representation is always logical individual frames:

```text
"head"       -> image
"eye_open"   -> image
"eye_closed" -> image
```

The editor never needs to know which packed sheet or rectangle a frame came from.

### 11.2 Packed representation on serialization

When Core serializes a project, it packs each character's frame library into one or more sprite-sheet pages.

For example:

```text
project.json
characters/
  <character-id>/
    artwork/
      page-0.png
      page-1.png
```

`project.json` records enough generated resource metadata to locate each frame in the packed pages:

```json
{
  "frames": {
    "eye_open": {
      "origin": [0.0, 0.0],
      "page": 0,
      "rect": [10, 20, 80, 30]
    },
    "eye_closed": {
      "origin": [1.0, -3.0],
      "page": 0,
      "rect": [92, 20, 72, 12]
    }
  }
}
```

Page number and rectangle are generated persistence metadata, not semantic identity.

Repacking is free to change them on every save.

### 11.3 Deserialization

On load, Core:

1. reads and validates `project.json`;
2. decodes the packed PNG page resources;
3. validates every recorded frame rectangle;
4. reconstructs the character-local logical frame library; and
5. presents logical individual frames to the rest of Core/editor code.

Reconstructing the logical frame library does not require copying every frame into a separate image buffer. Core may retain the decoded page images and represent loaded frames as regions within those shared buffers. If the user subsequently imports or modifies artwork, those new frames may instead be backed by standalone image data. Loaded and newly authored frames may coexist in the same frame library without repacking.

The loaded in-memory artwork does not semantically depend on its previous packing layout. Physical backing remains private to Core, and the frame/resource API must present sheet-backed and standalone frames uniformly. Saving may repack all current logical frames into a new set of sheets; this may change their physical backing and packed rectangles without changing frame names, registration origins, or appearance references.

### 11.4 No separate atlas JSON

A separate JSON file beside every sprite sheet is unnecessary.

`project.json` already contains the semantic character/artwork data and can also contain the generated page/rectangle metadata required to recover frame pixels.

### 11.5 Packing rules

Initial packing should favor correctness over maximum density:

- use a simple rectangle packer such as `stb_rect_pack`;
- never rotate frames while packing;
- reserve padding around each packed frame;
- extrude edge texels into the padding to avoid linear-filter sampling bleed;
- record only the true frame rectangle, excluding padding;
- leave unused page area transparent; and
- encode pages as alpha-preserving PNG.

Maximum page size and page-count policy are implementation choices.

### 11.6 Package implementation

Core already owns ZIP serialization through `miniz`. Artwork persistence should keep all archive/image/packing implementation private to Core.

A small private package reader/writer abstraction is appropriate once multiple archive entries are involved, so archive lifetime and error handling are not spread throughout `sm_project.cpp`.

Loading should remain atomic: malformed artwork data or resources must not partially replace the live project.

## 12. Runtime Packed-Sprite Access

Game/framework runtimes benefit from packed sprite sheets even though the editor does not.

Core should therefore provide a renderer-neutral runtime resource operation that produces or exposes:

- one or more packed sprite-sheet pages for a character;
- the rectangle for every named frame;
- each frame's registration origin; and
- enough metadata to associate a frame with its page.

The packed resource may be generated from the logical in-memory frame library, or Core may reuse an equivalent cached representation produced during load/save. Callers must not depend on a particular packing layout remaining stable.

A runtime-oriented result might conceptually look like:

```cpp
struct packed_frame_region {
    std::string name;
    std::size_t page;
    pixel_rect rect;
    point registration_origin;
};

struct packed_sprite_page {
    std::vector<std::uint8_t> png;
};

struct packed_artwork {
    std::vector<packed_sprite_page> pages;
    std::vector<packed_frame_region> frames;
};
```

The exact API may instead expose decoded RGBA page views if that better suits runtime integrations. The important boundary is that Core supplies packed pixels/layout while the host creates SDL/GPU/engine texture objects.

The editor does not consume this packed API for normal authoring.

## 13. Animation Integration

The semantic state model should exist before sprite animation is implemented so animation does not become coupled to one appearance's concrete image names.

The current `sm::animation` event variant contains skeletal rotation and translation events. When animation becomes character-owned and sprite-state animation is added, it can add a discrete semantic event conceptually like:

```cpp
struct sprite_state {
    std::string slot;
    std::string state;
};
```

An event means:

```text
slot "eyes" now has semantic state "closed"
```

It does not mean:

```text
show frame "human_eye_closed"
```

### 13.1 Evaluation

For a slot at time `t`:

- use the state from the latest `sprite_state` event for that slot at or before `t`;
- if there is no such event, use `default`.

The evaluated `(slot, state)` is then resolved through the active appearance's state-to-frame mapping.

### 13.2 Appearance independence

Because animation stores semantic states, the same animation works with different appearances:

```text
animation:
    eyes = closed

Human:
    closed -> human_eye_closed

Robot:
    closed -> robot_visor_dark
```

Switching appearance changes concrete imagery without changing animation timing or semantic state.

### 13.3 Validation

The animation editor should offer only states declared by the selected slot definition.

Slot/state rename or delete operations eventually need to update animation references as part of the same semantic edit.

The exact ownership/refactor of `sm::animation` is outside the appearances implementation, but this slot/state contract is the interface that artwork exposes to it.

## 14. View and Preview Controls

Artwork introduces two independent editor layers:

- rendered character artwork;
- skeleton/rig guides.

Useful view controls are:

```text
Show Artwork     [check]
Show Skeleton    [check]
Skeleton Display
    Normal
    Wireframe
```

When both are shown during authoring, the skeleton should normally be drawn as an editor guide above the artwork regardless of the artwork's internal painter order.

Previewing the final character is simply artwork on and skeleton off.

## 15. Suggested Implementation Phases

### Phase 1 — Character-owned artwork resources and persistence

Core:

- add `sm::artwork` directly to `sm::character`;
- add the named frame library and RGBA8 image resource abstraction;
- add frame registration origin;
- add slot definitions, appearances, appearance slots, and semantic-state mappings in memory;
- add narrow artwork access/semantic operations without reopening generic mutable character lookup;
- extend `.stickman` serialization for artwork;
- pack frame resources into PNG pages during serialization;
- reconstruct logical frames during deserialization without requiring standalone copies of sheet-backed images; and
- add atomic validation/error handling for artwork resources.

Editor:

- add the character-scoped Artwork Browser;
- add appearance create/rename/delete/switch;
- add frame import, rename, delete, thumbnails, and registration-origin editing; and
- expose basic slot/state data editing even before canvas rendering is complete.

Acceptance criteria: artwork survives save/reopen as character-owned logical frames and semantic appearance data; the editor never manipulates atlas pages directly.

### Phase 2 — Slot binding, rendering, painter order, and transform authoring

- create/rebind/delete slot definitions against bone IDs;
- support root/tip anchor selection;
- render the active appearance;
- resolve `default` and explicit state mappings;
- implement appearance-slot ordering as painter order;
- implement frame drag/drop onto bones;
- implement Sprite Transform mode;
- implement view controls; and
- surface unresolved slots clearly.

Acceptance criteria: the user can build a complete static multi-layer appearance, reorder its layers, pose the character, and see every sprite follow the intended bone and endpoint.

### Phase 3 — Semantic visual states

- add slot-state authoring UI;
- add state rename/delete propagation across appearances;
- support explicit hidden (`none`) targets;
- support missing-state fallback to `default`; and
- make state preview selectable in the editor even without an animation timeline.

Acceptance criteria: slots such as eyes can be previewed as `open`, `half`, and `closed`, and every appearance maps the same semantic vocabulary to its own frames.

### Phase 4 — Animation integration

- make animation ownership consistent with the character model;
- add `sprite_state(slot, state)` events;
- validate events against artwork slot definitions;
- evaluate persistent semantic slot state over time; and
- verify appearance switching during playback changes imagery but not semantic animation state.

### Phase 5 — Runtime packed-resource API

- expose packed sprite-sheet page resources and frame layout from Core;
- allow runtimes to create host textures without depending on editor code; and
- reuse/cache package packing internally where useful without making page layout semantic.

## 16. Testing Strategy

### 16.1 Artwork model

Automated tests should cover:

- character-local frame-name uniqueness;
- appearance-name uniqueness;
- slot-name uniqueness;
- mandatory `default` state;
- state-name uniqueness within a slot;
- appearance-slot reference validation;
- state-key validation;
- frame-target validation;
- one appearance slot per slot definition; and
- multiple slot definitions driven by the same bone.

### 16.2 Rename/delete

Test:

- frame rename propagation;
- slot rename propagation;
- state rename propagation;
- frame delete reference handling;
- slot deletion removing appearance implementations; and
- unresolved slot behavior after referenced-bone deletion/restoration.

### 16.3 Rendering semantics

Test known cases for:

- root anchor;
- tip anchor;
- identical root-to-tip orientation for both anchors;
- translation/rotation/non-uniform scale;
- negative-scale mirroring;
- frame registration origin;
- state fallback to `default`;
- explicit hidden state; and
- painter-order rendering according to appearance-slot vector order.

### 16.4 Persistence

Test:

- package round trip of frames, registration origins, slots, states, appearances, mappings, transforms, and slot order;
- alpha-preserving PNG encode/decode;
- repacking without semantic changes;
- packed frames never rotated;
- padding/extrusion rules;
- invalid page/rectangle rejection;
- missing page rejection;
- unknown frame/slot/state references rejected;
- duplicate artwork names rejected; and
- failed load leaving the existing project unchanged.

### 16.5 Runtime resources

Test:

- every logical frame appears exactly once in packed runtime layout;
- packed rectangles recover the same frame pixels;
- registration origins survive packing;
- multi-page output resolves correctly; and
- callers can consume runtime resources without Qt/editor dependencies.

### 16.6 Editor scenarios

Manually verify:

- artwork browser follows selected character/member topology;
- loose topology has no artwork context;
- bulk frame import and rename;
- creating several slots on one bone;
- dragging a frame onto a bone;
- root/tip switching;
- transform editing with skeleton fixed;
- appearance-slot drag reordering around overlapping joints;
- alternate-size eye frames aligned by registration origin;
- state preview across two appearances;
- unresolved slot indication and explicit rebinding;
- undo/redo across artwork edits; and
- save/reopen of a complete authored character.

## 17. Deferred Extensions

The initial model deliberately leaves several features for later without blocking them:

- per-state transform deltas;
- appearance-level preferred/default selection stored in the project;
- animation-driven appearance-slot reordering;
- tint/color modulation per slot;
- blend modes;
- clipping/masking;
- mesh deformation or weighted skinning;
- source-file reload metadata;
- editor support for recursive folder import;
- sprite-sheet page-size tuning; and
- cross-character resource sharing.

None of these require changing the basic relationship among slot definitions, appearance slots, semantic states, and named frames.

## 18. Intended Workflow

1. Create or select a character.
2. Open the Artwork Browser.
3. Import sprite frames into the character's frame library.
4. Create an appearance such as `Human`.
5. Select a bone and create a slot such as `torso`, `eyes`, or `left_hand`.
6. Choose the slot's root/tip anchor.
7. Add the slot to the active appearance and map its `default` state to a frame.
8. Repeat for the character's other visual layers.
9. Reorder the appearance slots until the painter-order overlap is correct.
10. Use Sprite Transform mode to position/rotate/scale each appearance slot.
11. Adjust frame registration origins where alternate source crops need alignment.
12. Add semantic states to stateful slots, for example `eyes = {default, open, half, closed}`.
13. Map those states to frames in the current appearance.
14. Create another appearance such as `Robot` and map the same slot/state vocabulary to different frames.
15. Pose the character and verify both appearances follow the rig correctly.
16. Save the project. Core packs the logical frames into character-scoped sprite-sheet pages inside the `.stickman` ZIP.
17. On reopen, Core reconstructs the logical frame library; the editor sees individual frames, not packed sheets.
18. When animation integration is added, animate semantic state changes such as `eyes = closed`; the active appearance resolves them to concrete imagery.

## 19. Design Summary

The complete semantic path is:

```text
RIG
bone object_id
    ^
    |
SLOT DEFINITION
name + bone + root/tip anchor + valid semantic states
    ^
    |
APPEARANCE SLOT
slot name + state->frame mapping + local T/R/S
    |
    |  ordered vector = painter order
    v
APPEARANCE

FRAME LIBRARY
frame name -> image + registration origin

ANIMATION (future)
(slot name, time) -> semantic state
```

The important boundaries are:

- bones provide stable project identity and pose;
- slot definitions provide stable character-artwork visual channels;
- appearances provide concrete visual implementations and painter ordering;
- semantic states allow animation to describe `eyes = closed` without naming a bitmap;
- frames are character-local named image resources;
- the editor works only with logical frames;
- Core packs frames into sprite sheets for persistence/runtime use; and
- packed page layout is generated resource data, never semantic identity.
