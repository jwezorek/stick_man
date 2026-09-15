# stick_man Artwork and Appearances

Current implementation — September 15, 2026

This document describes the artwork/appearance system as it exists now. Animation integration and a standalone runtime are intentionally out of scope; they will be designed when those systems are actually under development.

## 1. Overview

Artwork is owned directly by `sm::character`:

```text
character
├── rig
└── artwork
    ├── frames
    ├── slot definitions
    └── appearances
        └── ordered appearance slots
```

There is no project-level artwork registry and no cross-character sharing of image resources. Copying a character copies its artwork semantics; immutable image pixels may remain shared internally.

The model separates four ideas:

```text
sprite frame
    named bitmap resource + registration origin

slot definition
    named visual channel + driving bone + root/tip anchor + semantic states

appearance slot
    implementation of one slot in one appearance
    state -> frame/hidden mappings + local transform

appearance
    ordered appearance slots
    vector order is painter order
```

A static visual channel normally has only the mandatory `default` state. Stateful channels can add names such as `open`, `closed`, `smile`, or `fist`. These are semantic names shared by every appearance of the character.

## 2. Core Data Model

The implemented Core types are conceptually:

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
    double rotation = 0;       // radians
    point scale{1, 1};
};

using frame_target = std::optional<std::string>; // nullopt = explicitly hidden

struct appearance_slot {
    std::string slot;
    std::map<std::string, frame_target> states{{"default", std::nullopt}};
    sprite_transform transform;
};

struct appearance {
    std::vector<appearance_slot> appearance_slots;
};
```

`sm::artwork` owns three character-local namespaces:

- `frames()` — frame name -> `sprite_frame`;
- `slot_definitions()` — slot name -> `slot_definition`;
- `appearances()` — appearance name -> `appearance`.

Frame names, slot names, appearance names, and state names are strings because their identity is local to the containing artwork aggregate. Project/model objects such as characters and bones continue to use `sm::object_id`.

## 3. Frames and Image Resources

A frame is one logical bitmap resource available to every appearance of the character.

Core decodes images into renderer-neutral RGBA8 resources. Qt image objects, SDL textures, and GPU resources are not part of Core artwork semantics.

### 3.1 Registration origin

Each frame stores a registration-origin offset. The rendered image is otherwise centered on its local origin, so the default `{0, 0}` registration origin corresponds to the image center. Editing the registration origin lets differently cropped frames line up without changing every appearance-slot transform.

Registration coordinates use Core's Cartesian convention. Rendering converts the top-left-oriented pixel buffer appropriately.

### 3.2 Physical backing is private

A logical frame may be backed by a standalone decoded image or by a rectangular region of a decoded packed page. The rest of the program sees the same `image_resource` interface either way.

This allows loaded packed resources and newly imported images to coexist without forcing an immediate repack.

### 3.3 Frame operations

Core implements:

- insert/import;
- rename, with propagation through appearance mappings;
- delete, rejected while the frame is referenced;
- registration-origin editing.

The editor derives a unique frame name when importing images with colliding filenames.

## 4. Slot Definitions

A slot definition is a character-wide semantic visual channel. It says which bone drives the channel and which semantic states are valid.

Examples include:

```text
torso
eyes
mouth
left_hand
glasses
```

Several slots may be driven by the same bone. Slots are artwork data; bones themselves do not contain artwork slots.

### 4.1 Bone binding and root/tip anchor

A slot stores the stable `object_id` of its driving bone and an explicit root/tip anchor.

```text
root -> use the bone's root-node position
tip  -> use the bone's tip-node position
```

Both anchors retain the same local orientation:

```text
+X = root -> tip
```

Changing root to tip changes the frame origin, not the axis direction.

If topology editing removes the referenced bone, the slot is retained and becomes unresolved. Rendering skips unresolved slots. The Structure tab displays them as unresolved and allows rebinding to another bone.

### 4.2 Semantic states

Every slot must contain `default`. It cannot be renamed or deleted.

Additional states are optional and unique within the slot:

```text
eyes = { default, open, half, closed }
```

Adding, renaming, or deleting a state is a slot-definition operation. Rename/delete propagates to the corresponding mappings in every appearance.

## 5. Appearances and Appearance Slots

An appearance is one visual realization of a character, such as `Human`, `Robot`, or `Winter`.

An appearance contains zero or one implementation of each slot definition. The ordered `appearance_slots` vector is authoritative painter order: earlier items are drawn first and later items appear in front.

### 5.1 State mappings

Each appearance slot maps semantic states to frame targets.

There are three meaningful cases:

```text
state -> frame name     explicit image
state -> nullopt        explicitly Hidden
no mapping for state    Use default
```

`default` itself is always present in an appearance slot. Its target may be a frame or Hidden.

For a non-default state, absence of an explicit mapping means `resolve_frame()` falls back to the slot's `default` target. This is intentionally different from an explicit Hidden mapping.

If the appearance does not include a given slot at all, that slot renders nothing for that appearance.

### 5.2 Local transform

Every appearance slot stores one transform shared by all of its semantic states:

- translation X/Y;
- rotation in radians in Core and project data;
- independent X/Y scale.

Negative scale is valid and can be used for mirroring.

Bone length does not implicitly scale artwork.

## 6. Rendering Semantics

Core resolves a character/appearance plus semantic state choices into `resolved_sprite` values containing the image resource, registration origin, bone transform, and final transform.

Conceptually:

```text
bone endpoint position/orientation
    * appearance-slot translation
    * appearance-slot rotation
    * appearance-slot scale
    * registration-origin adjustment
```

The editor renders sprites in appearance-slot vector order. Skeleton guides are a separate editor layer and may be shown above the artwork without affecting sprite painter order.

Unresolved slots and slots absent from the active appearance do not produce drawables.

## 7. Artwork Browser

The Artwork Browser follows the selected character. Selecting a character, skeleton, bone, or node belonging to one character establishes that character as the artwork context. Loose topology or a mixed-character selection has no artwork context.

The browser currently has three tabs: **Structure**, **Appearances**, and **Images**.

### 7.1 Structure tab

The Structure tab edits character-wide slot definitions and state vocabularies.

The Slots view is multi-column:

```text
Slot | Bone | Anchor
```

- Slot names are renamed inline.
- The Bone column is edited with a combo box listing bones in the character, plus **Pick on canvas...**.
- The Anchor column is an inline Root/Tip combo box.
- **New slot...** creates a slot bound to a character bone.
- **Delete** removes the slot definition and its implementations from every appearance.

The selected slot's semantic states are shown below it. Non-default state names are renamed inline. **New state** and **Delete** manage the vocabulary; `default` remains protected.

### 7.2 Appearances tab

The Appearances tab selects and edits one appearance at a time.

Its tree has top-level slot rows and semantic-state children:

```text
✓ eyes
    ◉ default        eye_open
    ○ closed         eye_closed
✓ face
    default          face
⊘ glasses
    default          —
```

The green check / red prohibition icon on a top-level row toggles whether that slot is included in the active appearance.

The Image column for each included state is an inline combo box:

- a concrete frame name;
- **Hidden (none)**;
- **Use default (unmapped)** for non-default states.

For slots with more than one semantic state, the state rows also display radio-style preview bullets. They have one-of-N semantics within that slot. Clicking a bullet changes the editor's preview state through `artwork_layer::set_preview_state()`.

Static slots whose vocabulary contains only `default` do **not** show a preview radio indicator; there is nothing meaningful to switch.

Preview state is editor/session state. It is not persisted into the project and it does not create an undo command. Selecting `default` returns that slot to its normal preview state.

### 7.3 Painter order

Top-level appearance-slot rows can be dragged to reorder painter order. A compact toolbar also provides send-to-back, move-backward, move-forward, and bring-to-front operations.

Semantic-state child rows cannot be reordered independently because painter order belongs to the appearance slot, not to an individual state mapping.

### 7.4 Transform drawer

Transform editing is not a global tool in the tool palette. It is an explicit editing state owned by the Appearances tab.

The **Transform** button expands an in-pane drawer containing exact numeric controls for:

- Translation X;
- Translation Y;
- Rotation in degrees;
- Scale X;
- Scale Y;
- Reset transform.

Core continues to store rotation in radians; degree conversion is only an editor presentation detail.

Opening the Transform drawer also enables direct manipulation handles for the selected rendered appearance slot on the canvas:

- drag the center to translate;
- drag left/right side handles to scale X;
- drag top/bottom side handles to scale Y;
- drag corner handles for two-axis scaling;
- drag the rotation handle above the selection frame to rotate.

The skeleton stays fixed while these handles are used; transform editing does not invoke IK or mutate bone geometry.

A direct-manipulation drag previews continuously but commits as one artwork edit when the drag ends. Cancelling the drag discards the preview.

### 7.5 Selecting artwork on the canvas

Rendered sprites participate in artwork hit testing. Selecting a sprite selects its appearance slot and synchronizes the browser. The selection outline is shown whenever an artwork slot is selected; transform handles are shown only while Transform editing is active.

### 7.6 Dragging images onto bones

Frames in the Images tab can be dragged to the canvas.

Dropping a frame on a character bone either assigns it to a suitable existing slot or creates/uses a slot according to the current drop workflow. The operation validates that the frame belongs to the same character as the target bone and is committed as an artwork edit.

## 8. Images Tab

The Images tab displays the character-local frame library with thumbnails.

Current operations are:

- multi-file image import;
- rename;
- delete;
- registration-origin X/Y editing;
- drag a frame to the canvas for bone assignment.

Image file access belongs to the editor. Core receives encoded image bytes and owns decoding/validation.

## 9. Undo, Redo, Copy, and Topology Changes

Editor artwork mutations use `mdl::project::edit_artwork()`. The editor snapshots the character's `sm::artwork` before and after the semantic edit and records that as one undoable command.

This deliberately favors simple, reliable artwork undo semantics over a large family of tiny command classes. Immutable pixel resources make artwork copies inexpensive enough for the current scale.

Continuous direct transform dragging is special-cased so the intermediate mouse positions are preview-only and the completed drag becomes one undo step.

Whole-character copy/paste carries artwork with the character. Bone IDs referenced by slot definitions are remapped to the copied rig's new IDs.

Topology replacement also remaps artwork bone references when identities are intentionally replaced. Ordinary deletion can leave a slot unresolved rather than silently destroying its artwork semantics.

## 10. Project Persistence and Packing

Artwork is serialized inside the existing Core-owned `.stickman` ZIP package.

`project.json` contains artwork semantics and packed-frame metadata. PNG page resources live in character-scoped package paths.

Artwork serialization currently records:

- packed page filenames;
- logical frame names;
- each frame's page rectangle and registration origin;
- slot names, bone IDs, anchors, and state vocabularies;
- appearance names;
- ordered appearance slots;
- state mappings;
- translation/rotation/scale transforms.

There is no separate atlas JSON file.

### 10.1 Packing

`sm::artwork::pack()` produces `packed_artwork` containing PNG pages and logical frame regions.

Packing currently:

- uses `stb_rect_pack`;
- never rotates source frames;
- reserves padding around frames;
- extrudes edge pixels into the padding to avoid filtering bleed;
- records rectangles for the real frame pixels, excluding padding;
- supports multiple pages;
- uses transparent RGBA8 page storage.

Packed page placement is generated resource metadata, not semantic identity. Saving may repack frames without changing frame names, slots, mappings, or registration origins.

### 10.2 Loading

Core loads the package, decodes PNG pages, validates frame regions, reconstructs logical frames as image regions, then reconstructs slots and appearances through the normal validated artwork APIs.

Malformed artwork/package data causes load failure rather than partially mutating the live project.

## 11. View Controls

The View menu currently exposes:

```text
Show Artwork
Show Skeleton
Skeleton Display
    Normal
    Wireframe
```

Artwork visibility and skeleton-guide visibility are independent. Wireframe affects only the rig guide presentation.

## 12. Validation Rules

The current Core model enforces the important semantic invariants:

- frame, slot, and appearance names are non-empty and unique in their scopes;
- every slot contains `default` exactly once;
- state names are unique within a slot;
- `default` cannot be renamed or deleted;
- an appearance cannot contain the same slot twice;
- every appearance slot references an existing slot definition;
- every mapped state belongs to that slot's vocabulary;
- every named frame target exists;
- every appearance slot contains a `default` mapping;
- transforms and registration origins contain finite values;
- bone anchors are Root or Tip;
- imported frame dimensions fit the packing limits.

Semantic operations throw for invalid edits instead of allowing dangling internal artwork references.

## 13. Current Completion Boundary

The artwork/appearance authoring system is considered complete for the application's current scope.

Implemented now:

- character-owned image resources;
- slot definitions and bone binding;
- root/tip anchoring;
- multiple appearances;
- semantic state vocabularies;
- per-appearance frame/Hidden/fallback mappings;
- semantic-state preview in the editor;
- sprite rendering and canvas selection;
- explicit painter order;
- numeric and direct transform editing;
- registration-origin editing;
- undo/redo integration;
- character copy/remapping behavior;
- package persistence and sprite-page packing;
- artwork/skeleton display controls.

Not part of the current implementation boundary:

- animation/timeline events that drive semantic states over time;
- playback evaluation;
- a standalone game/framework runtime integration.

Those systems do not exist yet, so this document intentionally does not prescribe their APIs or implementation phases. The existing artwork model provides the semantic state and packed-resource foundations they can build on when that work begins.

## 14. Typical Authoring Workflow

1. Create or select a character.
2. Import bitmap frames on the Images tab.
3. Create slot definitions on the Structure tab and bind them to bones/root-or-tip anchors.
4. Add semantic states only where the visual channel needs them.
5. Create an appearance.
6. Include the desired slots in the appearance.
7. Choose a frame, Hidden, or default fallback for each state.
8. Reorder the top-level slot rows until painter order is correct.
9. Select a rendered slot and open the Transform drawer.
10. Position, rotate, and scale it numerically or directly on the canvas.
11. Adjust frame registration origins where alternate crops need alignment.
12. For stateful slots, use the radio bullets in the appearance tree to preview each semantic state.
13. Create additional appearances and map the same character-wide slot/state vocabulary to different frames.
14. Save the project; Core persists the semantics and packs the image resources into the `.stickman` package.
