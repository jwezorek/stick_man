**stick_man Artwork and Appearances Design**

Architecture, editor UX, bone-driven sprite artwork, project packaging, and phased implementation plan

Review draft — updated September 12, 2026

# 1. Executive Summary

The design goal is to add bone-driven bitmap artwork while keeping Core as the authoritative owner of both the semantic project model and the renderer-independent `.stickman` package format.

The current implementation already has the ownership boundary that artwork needs. `sm::project` owns `sm::topology` and the project's `sm::character` objects. Topology owns the live skeleton, node, and bone objects. Each character is the durable semantic object above that topology and directly owns its `sm::rig`, whose skeleton IDs resolve back into the project's topology. Artwork follows the same pattern: **artwork belongs to the character, not to the project and not to an individual skeleton**.

Conceptually, each `sm::character` gains a directly owned `sm::artwork` member. That artwork owns the character's logical sprite atlas and all of the character's appearances. The atlas is not a project-owned object referenced by ID. An appearance therefore does not need a `sprite_atlas_id`: its `sprite_id` values resolve against the atlas owned by the same character. Multiple appearances of one character share that character's atlas, but sprite atlases cannot be shared across characters.

This ownership is deliberate. A character remains stable while its rig may split into or merge several skeleton components. Character-owned artwork therefore survives ordinary topology edits without requiring resource transfer between transient skeletons. Deleting a character destroys its artwork with it; copying a whole character copies its artwork as character data rather than creating project-level shared resources.

The existing Core package implementation already serializes `project.json` into a ZIP-backed `.stickman` file using `miniz`. Artwork extends that existing format rather than introducing a new project root or a second persistence layer.

- Bones expose explicit string-valued sprite slots. These names are intentionally semantic and reusable across different appearances; ordinary node and bone display names remain cosmetic and non-load-bearing.
- A character owns one artwork object containing one logical sprite atlas and zero or more appearances.
- An appearance contains appearance items. Each item maps a string-valued bone slot to an opaque sprite ID in the owning character's atlas and stores local translation/rotation/scale, an explicit root-or-tip bone anchor, and explicit draw order.
- Sprite IDs are the load-bearing identity of concrete sprite resources. Sprite names are human-facing labels and do not participate in binding resolution; renaming a sprite therefore does not require rewriting appearance items.
- The editor presents logical sprites only. A character's `sm::sprite_atlas` may be backed by one or more packed atlas-page bitmaps; page count, packing, and rectangles remain Core resource details rather than rig semantics.
- The `.stickman` package contains `project.json` plus any packed RGBA8 PNG atlas pages and page metadata needed by character artwork. Core owns reading and writing this format so the editor and runtimes share one implementation.
- Core vendors the small implementation dependencies needed for persistence and image resources. The current implementation already vendors the JSON parser and `miniz`; artwork packaging adds `stb_image`, `stb_image_write`, and `stb_rect_pack` as private implementation dependencies.
- The Sprite Transform tool keeps the rig fixed and edits a selected sprite binding using translation, rotation, and scale handles.
- Sprite draw order is explicit per binding and user-editable; it is not inferred from binding/vector order.
- Sprite bindings use a bone-local frame whose +X direction runs from the bone root node to its tip node. Each binding explicitly anchors its origin at either the bone root or bone tip; choosing the tip changes the origin but does not reverse the local axes. Node sprite binding is not part of the design.

# 2. Design Goals and Boundaries

## 2.1 Core as a reusable library

Core is intended to be reusable by both:

- the Qt-based stick_man editor; and
- read-only stick_man runtime integrations for game frameworks and engines.

Accordingly, Core must not depend on Qt, SDL, or a particular renderer. Core owns renderer-independent project and resource services: `sm::project`, `sm::topology`, characters and rigs, character-owned artwork, ZIP package I/O, JSON serialization, RGBA8 image buffers, image decode/encode, sprite-atlas/page metadata, and rectangle packing. These services define and transport project data; they do not draw it.

| Concern | Core | Editor | Runtime / host integration |
|---|---|---|---|
| `sm::project` / topology / characters / rigs / IDs | Yes | Uses Core | Uses Core |
| Character-owned artwork and appearances | Yes | Authors them | Consumes them |
| Sprite slots and appearance items | Yes | Authors them | Consumes them |
| Character-local sprite atlas / sprite IDs / display names | Yes | Authors them | Resolves them |
| Binding T/R/S, root/tip anchor, and draw-order semantics | Yes | Edits them | Consumes them |
| Qt image objects | No | Yes | No |
| SDL / engine texture objects | No | No | Host-specific |
| Image decoding | Yes | Calls Core | Calls Core |
| Atlas-page rectangle packing | Yes; private save detail | Triggers through save | No |
| Drawing/rendering | No | Editor renderer only | Host renderer only |
| ZIP/package authoring | Yes | Calls Core | Normally read-only |
| RGBA8 image buffers / atlas resources | Yes | Consumes through Core | Consumes through Core |
| PNG atlas-page encoding | Yes; private save detail | Calls Core | No |
| ZIP/package reading | Yes | Calls Core | Calls Core |

## 2.2 What Core should know about sprites and resources

Core should know enough to describe bone-driven character artwork semantically and to own the renderer-independent resources used by the packaged project. The key Core-level concepts are:

- `sm::project` as the top-level Core-owned project object;
- `sm::topology` as the owner of live skeleton/node/bone topology;
- `sm::character` as the persistent semantic owner of one rig and its character-level resources;
- a directly owned `sm::artwork` member on each character;
- a symbolic string-valued sprite slot on a bone;
- one logical `sm::sprite_atlas` inside each character's artwork, containing sprites with opaque IDs and human-facing names;
- zero or more appearances inside that same artwork object;
- appearance items that map slot strings to sprite IDs in the owning character's atlas;
- the item transform and explicit root-or-tip bone anchor;
- explicit per-item draw order;
- one or more packed atlas pages and sprite rectangles as package/resource data; and
- the authoritative load/save rules for the `.stickman` package.

Atlas page indices and rectangles are not skeleton, rig, or appearance semantics: they may change whenever Core repacks a character's atlas. The stable resource relationship is:

```text
character
  -> artwork
     -> atlas
        -> sprite_id

character
  -> artwork
     -> appearance
        -> appearance_item.sprite_id
```

There is intentionally no project-level `sprite_atlas_id` hop. An appearance item resolves its sprite ID only within the artwork owned by the same character. Core should never know about `QImage`, `QPixmap`, `SDL_Texture`, GPU handles, or renderer-specific sampling state.

## 2.3 Core owns the packaged-project implementation

The `.stickman` package is already the canonical persisted form of a stick_man project. The current `sm::project::serialize()` implementation writes `project.json` into a ZIP archive with `miniz`, and `deserialize()` reconstructs topology, characters, rig membership, and skeleton parent links. Artwork should extend this Core-owned package rather than move persistence into the editor or create a separate artwork file format.

Core should expose project-, character-, and resource-level APIs in Core terms only. Third-party implementation types must not leak through public headers. In particular, callers should not see JSON-library objects, `stb_image` data structures, `stb_rect_pack` rectangles, or `miniz` archive handles.

The lightweight dependencies used for this implementation should remain vendored with the source rather than imposed as separately installed dependencies on Core consumers.

# 3. Identity and Indirection

## 3.1 Keep ordinary object names cosmetic

The recent ID refactor deliberately made ordinary node and bone names non-unique labels rather than structural identifiers. Artwork should not accidentally reverse that decision. Bones should therefore have a separate semantic sprite-slot property. Nodes do not need sprite slots because sprites bind only to bones.

```cpp
struct bone {
    object_id id;
    std::string name;        // display label; non-unique
    std::string sprite_slot; // semantic artwork interface; may be empty
    // ...
};

struct node {
    object_id id;
    std::string name; // display label; non-unique
    // ...
};
```

## 3.2 Sprite slots are intentionally string-valued

The purpose of a sprite slot is to provide stable symbolic indirection across interchangeable appearances. For example:

```text
Skeleton:
  upper-left-arm bone -> sprite slot "upper_arm_l"

Appearance "Human":
  "upper_arm_l" -> sprite "upper_arm"

Appearance "Robot":
  "upper_arm_l" -> sprite "armored_upper_arm"

Appearance "Skeleton":
  "upper_arm_l" -> sprite "humerus"
```

The slot is therefore deliberately more load-bearing than a display name. This is a feature, not an accidental identity mechanism.

## 3.3 Sprite IDs are resource identity; sprite names are labels

Each sprite in a character's `sm::sprite_atlas` has an opaque `object_id` and a string name. The ID is the stable identity used by appearance items and package/resource lookup. The name exists for users, import defaults, browser display, debugging, and similar human-facing purposes; it is not load-bearing and need not be globally unique.

```text
character.artwork.atlas + appearance_item.sprite_id -> sprite
sprite.name                                            -> display label only
```

This keeps strings only at the deliberate late-binding boundary between rig semantics and an appearance: bones expose slot strings and appearance items match those strings. Once an appearance has selected a concrete sprite resource for a slot, ordinary opaque IDs are sufficient. Renaming a sprite does not alter bindings or atlas packing.

Because the atlas is a direct member of one character's artwork, a sprite ID is not a handle to a project-owned sprite sheet. No character can bind to another character's atlas by storing an atlas ID. Cross-character sharing is intentionally unsupported; copying artwork between characters is a copy/import operation, not shared ownership.

# 4. Proposed Core Data Model

The exact C++ names can follow existing Core conventions, but the ownership model should match the implementation already established for characters and rigs. Conceptually:

```cpp
struct sprite_transform {
    point translation{};
    float rotation = 0.0f; // radians internally and in JSON
    point scale{1.0f, 1.0f};
};

enum class bone_anchor {
    root,
    tip
};

struct sprite {
    object_id id;
    std::string name; // display label; non-load-bearing
    std::uint32_t page = 0; // current packed atlas page
    rect source_rect; // current rectangle within that page
};

struct sprite_atlas {
    std::vector<sprite> sprites;
    std::vector<atlas_page> pages;
};

struct appearance_item {
    std::string slot;       // matches a bone sprite_slot
    object_id sprite_id;    // concrete sprite in this character's atlas
    bone_anchor anchor = bone_anchor::root;
    sprite_transform transform;
    int draw_order = 0;     // lower draws first
};

struct appearance {
    object_id id;
    std::string name; // display label
    std::vector<appearance_item> items;
};

struct artwork {
    sprite_atlas atlas;
    std::vector<appearance> appearances;
};
```

The important ownership relationship is the character itself:

```cpp
class character {
    const object_id id_;
    std::string name_;
    std::reference_wrapper<project> owner_;
    sm::rig rig_;
    sm::artwork artwork_; // directly owned; never a project-level reference
};
```

This extends the current implemented shape of `sm::character`, which already directly owns its `rig_`. `sm::project` continues to own topology and character lifetime; topology continues to own skeletons, nodes, and bones. Artwork does not move into topology and is not entered as a separate project-owned aggregate merely so that a character can refer to it.

A character always has one artwork member, even when it is empty. Its appearances share that character's logical atlas. Consequently:

- no `sprite_atlas_id` is required on an appearance;
- no project-level atlas table is required;
- an appearance item may only reference a sprite in its own character's atlas;
- atlas lifetime is exactly character lifetime;
- deleting a character cannot leave dangling appearance/atlas references elsewhere in the project; and
- two characters cannot share a sprite sheet by reference.

The atlas remains logical and may serialize to multiple packed bitmap pages. Page and rectangle fields are resource metadata and may be regenerated without changing sprite IDs, appearance IDs, slot strings, or bindings.

## 4.1 Active appearance

“Active appearance” should primarily be editor/session or runtime-instance state, not mutable state on the Core skeleton, topology, or character. A runtime may render multiple instances of one character with different appearances or change appearances dynamically.

Core APIs should therefore evaluate a selected appearance **within a selected character** rather than require the character itself to own a mutable active-appearance field. The character owns the set of available appearances; a particular editor canvas or runtime instance chooses which one to display.

A character may eventually contain a preferred/default appearance as persisted metadata, but that should remain conceptually distinct from the active appearance of a particular editor or runtime instance.

## 4.2 Renderer-neutral image access

Core image data should use one simple canonical pixel representation: RGBA8. The public API should provide a lightweight non-owning view rather than expose `stb_image` or renderer-specific image objects. Conceptually:

```cpp
struct image_view {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t stride = 0; // bytes per row
    const std::uint8_t* pixels = nullptr; // RGBA8
};

struct sprite_region {
    image_view page;
    rect source_rect; // actual sprite pixels within page
};
```

A runtime can upload returned atlas-page pixels into its own texture type and use `source_rect` for sampling. The editor can use the same data for thumbnails and canvas rendering through Qt. Core owns the lifetime of the backing image buffer through the character's artwork; callers receive read-only views.

Imported sprites may be backed by standalone RGBA8 buffers while being authored. Packing may assign them to one or more atlas pages and update page/rectangle resource metadata. This storage distinction should be hidden behind Core resource-resolution APIs so page layout never becomes semantic identity.

# 5. Sprite Binding Transform Semantics

## 5.1 Canonical bone-local orientation

A sprite binding must be independent of the pose in which it was authored. The stored transform therefore uses a canonical local orientation derived from the bone:

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

The zero-angle direction always follows the bone from its root node to its tip node. The sign convention for +Y follows the coordinate-system conventions already used by Core and should be documented in the implementation. The binding origin is supplied separately by its root/tip anchor.

## 5.2 Root and tip anchors

Every sprite binding explicitly chooses one endpoint of its bone as its origin. A root-anchored binding uses the current root-node position; a tip-anchored binding uses the current tip-node position. Both anchors use the same root-to-tip orientation, so choosing tip changes translation only and never reverses the axes.

```text
root anchor origin = current root-node position
tip anchor origin  = current tip-node position
orientation        = current bone angle, root -> tip
```

Root/tip anchoring removes the need for node sprite binding: artwork that belongs at a joint can be bound to an incident bone and anchored at the endpoint coincident with that joint. It also keeps distal artwork, such as a hand or foot, attached to the correct endpoint if bone length changes.

## 5.3 Default binding

When a sprite is first bound to a bone, the default mapping should be:

- sprite center coincident with the selected root-or-tip anchor;

- sprite rotation = 0 radians relative to the bone-local +X direction;

- translation = (0, 0); and

- scale = (1, 1).

## 5.4 Evaluation

At runtime or during editor rendering, the bound bone supplies a world-space orientation and the selected anchor supplies the world-space origin. The final sprite transform is ordinary transform composition:

```text
binding_world = translate(current_anchor_position)
              * rotate(current_bone_angle)

sprite_world  = binding_world
              * binding_local_translation
              * binding_local_rotation
              * binding_local_scale
```

Bone length should not automatically scale a sprite in the initial implementation. If bone length changes, a root-anchored sprite remains at the root and a tip-anchored sprite follows the tip; neither is implicitly stretched. Stretch behavior can be designed explicitly later if needed.

Rotation values should be stored in radians in Core and in project JSON, consistent with the rest of the system. The editor may present degrees in numeric controls and convert only at the UI boundary.

# 6. Editor Artwork Browser

## 6.1 Presentation

The Artwork Browser is character-scoped. It should show and edit the artwork owned by the currently selected/active character; when there is no character context, the pane can be disabled or show an explanatory empty state. Loose skeletons do not independently own artwork.

The browser does not need a tree view unless future requirements introduce hierarchy deeper than appearances and the sprites in the character's atlas. An appearance combo box plus an icon/thumbnail view of the character's shared logical atlas is the simpler model:

```text
Character:  Alice
Appearance: [ Human v ] [New Appearance v] [Actions ...]

+----------------------------------------------------+
| [thumbnail] [thumbnail] [thumbnail]                |
| Head        Torso       Upper Arm                  |
|                                                    |
| [thumbnail] [thumbnail] [thumbnail]                |
| Forearm     Hand        Thigh                      |
|                                                    |
| Drop image files here                              |
+----------------------------------------------------+
```

A `QListView` in icon mode or an equivalent thumbnail view is a natural Qt implementation. The user sees logical sprites by name and thumbnail, never packed atlas pages or rectangles.

## 6.2 Appearance creation and sprite import

Appearance creation and management should live in the Artwork Browser rather than primarily in the application menu bar. The selected character already owns the atlas, so creating an appearance never creates or attaches a separate atlas.

| User operation | Behavior |
|---|---|
| New Appearance -> Empty Appearance | Add an empty appearance to the selected character's artwork. The character's existing atlas is unchanged. |
| New Appearance -> Appearance from Folder... | Add an appearance, then import all supported image files from one selected directory into that character's atlas. Initial implementation should not recurse into subdirectories. |
| Drag image file(s) into browser | Import one or several images into the selected character's atlas. |
| Add Image... | Open a file picker; allow one or multiple supported image files and add them to the selected character's atlas. |
| Add Folder... | Import all supported image files from a selected directory into the selected character's atlas. |
| Optional: drag directory into browser | Can later be made equivalent to Add Folder... if useful. |

Importing a sprite adds a concrete sprite resource to the selected character's atlas. It does not automatically create an appearance item or bind the sprite to a bone slot.

A sprite from one character is not a live resource that another character can reference. If artwork needs to move or be reused across characters, the editor should perform an explicit copy/import that creates sprite resources in the destination character's atlas.

## 6.3 Sprite identity and naming

On import, Core assigns the sprite a new opaque ID and the editor derives a default display name from the source filename without its extension. Because appearance items refer to sprite IDs, names are not semantic resource keys and renaming a sprite requires no reference propagation. The editor may still choose unique-ish default names for readability, but uniqueness is not a Core invariant.

The editor may optionally retain the source filesystem path as transient authoring metadata for a future “Reload from Source” operation, but saved projects must not depend on external paths.

# 7. Binding Sprites to Bones

## 7.1 Bone sprite slots

A user should be able to assign or edit a bone sprite slot, for example through the selected bone’s context menu or property editor:

```text
Sprite Slot >
  None
  head
  torso
  upper_arm_l
  forearm_l
  ...
  New Slot...
```

## 7.2 Dragging a sprite onto a bone

Dragging a sprite from the active character's Artwork Browser onto a bone in that character's rig should create or replace the active appearance's item for the bone's sprite slot. The item records the slot string and the dragged sprite's opaque ID; the sprite must belong to the atlas directly owned by that same character. The item also stores the explicit root-or-tip anchor, local T/R/S, and draw order.

The browser/drag source should normally make a cross-character binding impossible. If the UI nevertheless receives a sprite from another character, Core must reject the binding rather than create a hidden cross-character resource reference. The sprite must first be copied/imported into the destination character's artwork.

Recommended behavior when the target bone does not yet have a sprite slot: automatically offer/create a slot, using a sensible default name. The exact naming rule remains a review item; candidates include the sprite name or a normalized form of the target display name.

## 7.3 Root/tip anchor selection

The editor must let the user choose whether a sprite binding is anchored at the bone root or bone tip. This makes joint-centered artwork possible without node bindings: a knee-cover sprite can be bound to either the thigh or lower-leg bone and anchored at whichever endpoint coincides with the knee, thereby choosing both its joint position and the bone whose rotation it follows.

- The root/tip choice is an explicit property of the binding and must be persisted.

- Changing a binding from root to tip changes its frame origin but keeps the local +X axis pointing root -> tip.

- The initial anchor chosen when a sprite is first dropped on a bone remains a small editor-UX decision; the binding must always be user-editable afterward.

## 7.4 Why slot strings and sprite IDs are different

A sprite slot expresses rig semantics and is intentionally symbolic; a sprite ID identifies one concrete resource in the owning character's atlas. Keeping this boundary string-based on the skeleton side and ID-based on the resource side allows:

- different appearances to map the same semantic slot to different concrete sprite IDs;

- two slots to reuse one sprite ID when appropriate (for example, both eyes using one sprite with different transforms); and

- sprite display names and atlas packing to change without renaming the skeleton interface or rewriting bindings.

## 7.5 Explicit draw order

Every sprite binding has an explicit `draw_order` value. Draw order is important authoring data for articulated cutout artwork: overlapping limb pieces and joint-cover sprites depend on predictable painter's order to conceal seams. The order must not be inferred from the binding vector or atlas packing order.

- Lower `draw_order` values are rendered first; higher values are rendered later and therefore appear in front.
- The editor should maintain a deterministic order and avoid ambiguous ties, for example by keeping `draw_order` values unique within an appearance and resequencing when necessary.
- The user should be able to edit draw order explicitly, with a numeric property and convenience actions such as Bring Forward, Send Backward, Bring to Front, and Send to Back.
- Draw order belongs to the character-owned appearance semantic data, not to sprite-atlas page metadata.

# 8. Sprite Transform Tool

The Sprite Transform tool is an editor mode for modifying the binding transform while treating the skeleton as fixed reference geometry.

- The skeleton remains fixed; dragging must not move nodes, change bone geometry, or invoke IK.

- The user selects a sprite-bound bone or its sprite.

- Only the selected sprite displays manipulation widgets to avoid visual clutter.

- Dragging the sprite changes local translation.

- A rotation handle changes local rotation.

- Corner or edge handles change scale.

- The data model should support non-uniform X/Y scale even if the default UI emphasizes uniform scaling.

- Exact numeric T/R/S values should also be available in the properties pane while the tool is active. Rotation may be displayed in degrees for user convenience while Core and JSON store radians.

An arbitrary custom pivot is not required in the first implementation. Rotation about the sprite center plus an editable translation relative to the selected bone endpoint should be sufficient initially. The binding anchor supplies either the bone root or bone tip as the frame origin.

# 9. View and Rendering Controls

Bone-driven sprite artwork introduces two independent visual layers: artwork and rigging guides. The View menu should make these explicit.

```text
View
  Show Sprites [check]
  Show Skeleton [check]
  Skeleton Display >
    Normal
    Wireframe
```

Recommended behavior: when both sprites and skeleton are visible during editing, draw the skeleton as a foreground guide, with wireframe presentation available (and likely preferable during sprite placement). Previewing the final character is simply Show Sprites = on and Show Skeleton = off.

Within the sprite layer itself, bound sprites are rendered strictly by each binding's explicit draw_order. This semantic sprite ordering is independent of the editor-only decision to draw skeleton guides above or below the completed sprite layer.

A general-purpose user setting for arbitrary sprite-over-bone versus bone-over-sprite ordering is not necessary for the first version. Internally, keeping rendering order flexible is inexpensive and leaves room for future editor display modes.

# 10. Project Package Format

## 10.1 Container

The current implementation already saves `.stickman` projects as ZIP archives owned by Core. `sm::project::serialize()` writes a `project.json` entry with `miniz`, and `deserialize()` reads the same archive back. Artwork should extend this existing package.

```text
character.stickman  // physically a ZIP archive
```

Users continue to treat the package as one project file, while advanced users can inspect it with ordinary ZIP tools.

## 10.2 Suggested internal layout

Because artwork is owned by characters, package resource paths should mirror that ownership rather than use a project-level atlas directory. One straightforward layout is:

```text
project.json
characters/
  <character-id-1>/
    artwork/
      page-0.png
      page-0.json
      page-1.png
      page-1.json
  <character-id-2>/
    artwork/
      page-0.png
      page-0.json
```

Each character has one logical atlas that may span any number of packed bitmap pages. There is no package-level atlas object that two characters point at. Repacking changes page images and rectangle metadata only; sprite IDs, appearance IDs, slot strings, appearance-item references, character IDs, and rig membership remain unchanged.

## 10.3 `project.json` responsibilities

`project.json` remains the authoritative semantic document within the Core-owned package format. The current format already stores the project version, topology, and characters with their rig skeleton IDs. Artwork extends each character record with its directly owned artwork semantic data.

Conceptually, each serialized character contains:

```json
{
  "id": "<character-id>",
  "name": "Alice",
  "skeletons": ["<skeleton-id>", "<skeleton-id>"],
  "artwork": {
    "sprites": [
      {"id": "<sprite-id>", "name": "head"}
    ],
    "appearances": [
      {
        "id": "<appearance-id>",
        "name": "Human",
        "items": []
      }
    ]
  }
}
```

The exact JSON schema can follow existing serialization conventions, but it should contain or reference:

- format version;
- topology contents, including skeleton structure and persistent object IDs;
- character IDs, names, and rig membership;
- each character's sprite definitions, sprite IDs, and sprite display names;
- each character's appearance definitions and appearance IDs;
- bone sprite slots and appearance items once binding is implemented;
- each appearance item's `sprite_id`, binding transform, explicit root/tip anchor, and explicit draw order;
- poses and animations when those systems are added; and
- the character-local atlas-page resource files necessary to resolve each sprite ID to pixels.

No appearance stores a project-level `sprite_atlas_id` because its atlas is implied by character ownership.

## 10.4 Atlas JSON responsibilities

Every packed atlas-page image should have sibling JSON metadata. That metadata answers only: “which sprite IDs currently occupy which rectangles on this page?” It may include sprite names as human-readable/debugging metadata, but IDs are authoritative. It should not contain skeleton slots, appearance items, transforms, root/tip anchor choices, draw order, pivots, or other rig semantics.

```json
{
  "format_version": 1,
  "image": "page-0.png",
  "width": 2048,
  "height": 2048,
  "sprites": {
    "7f1d...": {
      "name": "forearm",
      "x": 100,
      "y": 20,
      "width": 80,
      "height": 220
    },
    "a93b...": {
      "name": "head",
      "x": 220,
      "y": 20,
      "width": 180,
      "height": 190
    }
  }
}
```

Atlas-page entries are keyed by opaque sprite ID. A sprite name may be repeated or changed without affecting identity. The containing character determines which logical atlas the page belongs to; page assignment and rectangles are current packing metadata.

## 10.5 Packing ownership

Rectangle packing is a private Core persistence/resource function. When a project is saved, Core may repack each character's `artwork.atlas` into one or more atlas pages. The editor simply requests that Core save the `sm::project`. Read-only runtimes do not invoke packing, but they use the same Core package loader and resource-resolution APIs.

On load, Core reads the package, reconstructs topology and characters first, reconstructs each character's artwork, decodes that character's atlas-page PNGs into RGBA8 buffers, validates sprite-ID/page/rectangle metadata, and exposes logical sprites through renderer-neutral resource views. The editor and runtimes do not need their own ZIP, PNG, or atlas parsers. Because stick_man is not a bitmap editor, the package does not need to reconstruct original individual source files on disk.

Character lifetime and artwork lifetime are the same. Loading does not populate a project-level atlas table and then reconnect characters to it; it directly constructs the artwork inside each character.

## 10.6 Atlas packing rules

The initial atlas implementation should deliberately favor simplicity and predictable sampling over maximum packing density:

- Use `stb_rect_pack` for rectangle placement.
- Never rotate sprites during packing. Their width and height remain in authored orientation, and atlas metadata needs no rotated flag.
- Reserve a one-pixel transparent gutter around every sprite by packing a rectangle two pixels wider and two pixels taller than the actual sprite, then placing the sprite at +1,+1 within that reserved rectangle.
- Initialize atlas pages as transparent RGBA8 `(0,0,0,0)`, so all unused pixels and gutters remain transparent.
- Store only the actual sprite-pixel rectangle in page metadata; gutter pixels are an internal packing detail and are excluded from x/y/width/height.
- Write atlas pages as PNG with alpha preserved.

The one-pixel border on each packed rectangle means adjacent sprite content will normally have two transparent pixels between it. This small amount of extra atlas space is intentional and avoids texture-filtering bleed without complicating runtime sampling.

## 10.7 Vendored implementation dependencies

Core should vendor the small source/header dependencies required by the project format. The intended set is:

- the existing vendored JSON parser for `project.json` and atlas JSON;
- the existing vendored `miniz` for ZIP archive reading and writing;
- `stb_image` for decoding imported/package image files into RGBA8 buffers;
- `stb_image_write` for encoding RGBA8 atlas pages as PNG; and
- `stb_rect_pack` for non-rotating atlas rectangle packing.

These are Core implementation dependencies only. Consumers linking against Core should not need Qt, SDL, libpng, zlib, or a separately installed JSON/ZIP/image package merely to load a stick_man project.

# 11. Runtime Resource Resolution

A read-only runtime needs to resolve an appearance item's sprite ID into pixels and a source rectangle. Because artwork is character-owned, the character itself supplies the resource scope:

```text
(character, appearance, appearance_item.sprite_id)
              |
              v
character.artwork.atlas
              |
              v
Core atlas/resource resolution
              |
              v
(RGBA8 atlas-page image_view, source rectangle)
              |
              v
host creates texture + Core-computed appearance-item transform
```

There is no `appearance.sprite_atlas_id` lookup. The selected appearance already belongs to the character whose atlas contains all sprites that the appearance may reference.

A Cephalopod/SDL integration can create SDL textures from Core-provided RGBA8 atlas-page buffers and use the returned source rectangles. The Qt editor can wrap the same buffers in its own image/texture objects. Another engine integration can upload or preprocess them differently. Core remains renderer-agnostic because it supplies bytes and geometry, not Qt, SDL, or engine texture types.

# 12. Phased Implementation Plan

The original draft assumed that `sm::project`, the packaged project, and character-independent artwork resources still needed to be introduced. The current code has moved past that baseline: `sm::project`, `sm::topology`, `sm::character`, `sm::rig`, character membership rules, and ZIP-backed `.stickman` serialization already exist. The artwork work should build on those objects rather than reintroduce `sm_world` or project-owned atlases.

## Phase 1 — Character-owned in-memory artwork and Artwork Browser

Goal: add the in-memory artwork model directly to `sm::character` and provide editor-side appearance/sprite authoring without yet binding sprites to bones.

- Add `sm::artwork` and make it a direct member of `sm::character`, alongside `rig_`.
- Give each artwork object one logical `sm::sprite_atlas` and a collection of appearances. Do not add project-level atlas or appearance ownership tables.
- Add sprite IDs/names and the renderer-neutral RGBA8 image resource/view abstraction; keep Qt image/texture objects on the editor side.
- Ensure every newly created character begins with an empty artwork member.
- Add the Artwork Browser UI scoped to the selected/active character: appearance combo box, New Appearance control, appearance actions, and thumbnail/icon view of the sprites in that character's atlas.
- Implement Empty Appearance creation without creating a new atlas.
- Implement Appearance from Folder..., adding the appearance and importing all supported image files into the same character-owned atlas without recursion.
- Implement Add Image... with multi-selection.
- Implement Add Folder....
- Decode imported images through Core using vendored `stb_image` and normalize them to RGBA8.
- Implement drag-and-drop of one or more image files onto the sprite browser.
- Assign a new opaque ID to every imported sprite and derive a readable default name from its filename. Names need not be unique at the Core level.
- Implement basic appearance and sprite management such as rename/delete as needed for a usable browser. Deleting a sprite must validate or repair appearance-item references once binding exists.
- Maintain editor-only active character/appearance state.
- Extend whole-character clipboard behavior so that character data is copied deeply, including artwork; do not share the source character's atlas with the pasted character.

Explicitly out of scope for Phase 1:

- persistence of artwork and imagery in the `.stickman` archive;
- atlas-page packing;
- bone sprite slots;
- sprite-to-bone bindings;
- sprite rendering on the rig; and
- the Sprite Transform tool.

Phase 1 acceptance criteria: multiple characters can each have independent artwork; the user can create multiple appearances for one character, switch between them, populate the character's shared atlas through every planned import path, see thumbnail sprites, and rename/delete content in memory. Creating or editing artwork for one character has no effect on another character.

## Phase 2 — Extend packaged-project serialization for character artwork

Goal: extend the existing Core-owned `.stickman` ZIP format so character-owned artwork and imagery round-trip with the topology and current character/rig data.

- Bump the project format version when the artwork schema lands.
- Extend each serialized character entry in `project.json` with its artwork semantic data rather than adding project-level atlas/appearance tables.
- Serialize sprite IDs/names and appearance IDs/names inside the owning character's artwork.
- Add Core-side rectangle packing with vendored `stb_rect_pack` and generate one or more RGBA8 atlas-page images per character; do not rotate sprites and reserve a one-pixel transparent gutter around each sprite.
- Write each character's page images and page JSON under character-scoped archive paths.
- Encode atlas-page images as alpha-preserving PNGs with vendored `stb_image_write`; continue writing the archive with the existing vendored `miniz` implementation.
- On deserialize, reconstruct each character and its rig membership, then reconstruct that character's artwork and decode its page images with `stb_image`.
- Validate sprite IDs, page rectangles, appearance items, and character-local resource boundaries. An appearance item must never resolve to a sprite owned by another character.
- Ensure repacking does not change character identity, rig membership, sprite IDs, appearance identity, slot strings, or semantic bindings.
- Add corruption/error handling for missing character artwork files, missing pages, unknown sprite IDs, invalid rectangles, duplicate IDs where uniqueness is required, and unsupported package versions.

Phase 2 acceptance criteria: closing and reopening a packaged project through Core reproduces the same topology, characters, rig memberships, character-owned sprite resources, appearances, and imagery. No post-load project-level reconnection of atlas references is required, because the artwork is reconstructed directly inside its character.

## Phase 3 — Bone sprite binding, root/tip anchors, draw order, and Sprite Transform tool

Goal: make appearances drive visible character artwork and provide the complete authoring workflow for bone binding, endpoint anchoring, placement, and explicit layering.

- Add the explicit string-valued `sprite_slot` property to bones and expose it in the editor (context menu and/or properties UI). Nodes do not receive sprite slots.
- Add Core appearance-item data: slot string -> `sprite_id` plus explicit `bone_anchor` (root or tip), local translation/rotation/scale, and explicit `draw_order`.
- Validate that every `sprite_id` resolves in the atlas owned by the same character as the appearance and target rig.
- Extend project serialization to persist bone sprite slots, appearance items, sprite IDs, root/tip anchors, T/R/S (rotation in radians), and explicit draw order.
- Render the active character/appearance's bound sprites on the editor canvas by matching bone slot strings to appearance items, resolving each `sprite_id` through `character.artwork.atlas`, and sorting items by explicit `draw_order`.
- Implement drag-and-drop from the Artwork Browser onto a bone to create or replace a binding.
- Reject cross-character sprite bindings rather than creating shared resource references.
- Finalize behavior for dropping onto a bone that has no sprite slot yet, and finalize the initial root/tip choice on drop.
- Implement the canonical bone-local orientation: zero-angle +X runs from root node to tip node.
- Implement explicit root/tip binding anchors. Root uses the root-node position; tip uses the tip-node position; both retain the same root-to-tip axes. Do not implement node sprite binding.
- Implement the default center-to-anchor, zero-rotation, unit-scale binding for bone targets.
- Implement the Sprite Transform tool with rig locking, sprite selection, and translation/rotation/scale manipulation handles for bone-bound sprites.
- Expose exact numeric binding T/R/S values in the property UI; display rotation in degrees if desired while persisting radians.
- Add View commands for Show Sprites, Show Skeleton, and Normal/Wireframe skeleton display.
- Implement explicit per-binding `draw_order` in Core, serialization, rendering, and editor controls. Provide numeric editing plus Bring Forward, Send Backward, Bring to Front, and Send to Back actions; draw order must not depend on vector order.

Phase 3 acceptance criteria: the user can assign semantic sprite slots to bones, drag sprites from the active character's atlas onto bones in that character's rig, select root or tip anchoring, explicitly control sprite draw order, adjust each appearance item with T/R/S controls while the rig stays fixed, switch active appearances, pose/animate the character, and see artwork follow the correct bone endpoint and orientation with predictable overlap. All slot strings, sprite-ID references, anchors, transforms, and draw-order data round-trip through the packaged project format.

# 13. Recommended Testing Strategy

Even without a large GUI unit-test suite, the design creates several testable non-GUI seams. Useful automated coverage would include:

- character ownership invariants: each character directly owns one artwork object and no artwork object is reachable as a shared project-owned atlas resource;
- isolation between two characters with similarly named appearances/sprites: editing, deleting, repacking, or renaming one character's artwork cannot affect the other;
- whole-character copy/paste deep-copy behavior, including fresh character identity and non-shared artwork resources;
- character deletion destroying artwork without leaving any external atlas/appearance reference to repair;
- rig split/merge/adoption behavior leaving the character's artwork exactly where it is, because artwork follows character identity rather than individual skeleton components;
- package round-trip tests for `project.json`, character-owned artwork metadata, atlas-page JSON, and image resources;
- RGBA8 PNG encode/decode round trips that preserve alpha;
- atlas packing invariants: sprites are never rotated, each has the required transparent gutter, stored rectangles exclude padding, and repacking may change page/rect without changing sprite IDs;
- package loading through Core without Qt or a renderer present;
- atlas validation and missing/corrupt page or unknown-sprite-ID handling;
- explicit rejection of an appearance item that attempts to reference a sprite outside its owning character's atlas;
- bone-local transform math for known root-anchor and tip-anchor cases;
- root/tip anchor behavior, including the invariant that both anchors retain the same root-to-tip orientation;
- binding transform composition for translation, rotation, and non-uniform scale;
- draw-order sorting and editor resequencing invariants;
- sprite-slot resolution across two appearances of the same character that reuse the same slot names but map them to different sprite IDs; and
- repacking invariants: character IDs, rig membership, sprite IDs, appearance IDs, slot strings, and appearance-item references remain unchanged after packing layout changes.

GUI behavior can then be validated with focused manual scenarios: switching character selection in the Artwork Browser, importing images, switching appearances, verifying that another character's browser contents remain independent, drag/drop onto bones, switching bindings between root and tip anchors, transform-handle interaction, numeric T/R/S editing, draw-order changes around deliberately overlapping joints, view toggles, save/reopen, and a few deliberately asymmetric poses that make incorrect transforms obvious.

# 14. Review Items / Decisions Still Open

| Decision | Current recommendation / status |
|---|---|
| Project file extension | Resolved in current code: `.stickman`, physically a ZIP archive. |
| Drop onto bone with no sprite slot | Create/offer a slot automatically. Exact default naming rule still needs to be chosen. |
| Initial root/tip anchor on drop | Still open as an editor-UX detail. The binding is always explicitly root or tip and remains user-editable afterward. |
| Sprite naming and identity | Resolved: every sprite has an opaque ID used for references. Sprite names are human-facing labels, are not load-bearing, and need not be unique at the Core level. |
| Supported image formats | Core decodes through vendored `stb_image` and normalizes to RGBA8. PNG is the required package format; the exact set of additional import formats may remain limited by policy. |
| Packing algorithm and atlas-page limits | Packing algorithm resolved for the initial implementation: vendored `stb_rect_pack`, no 90-degree rotation, one-pixel transparent gutter around every sprite. A character's logical atlas may be backed by multiple packed atlas pages; maximum page size / multi-page policy remains to be chosen. |
| Custom sprite pivot | Defer. Start with sprite-center rotation plus translation and scale relative to the selected root/tip anchor. |
| Binding draw order | Resolved: explicit per-binding integer `draw_order`, lower drawn first; user-editable and not inferred from container order. |
| Legacy project compatibility | Current character design does not require backward compatibility with older project files. Artwork serialization may bump the format version rather than add a migration path unless that requirement changes. |
| Default appearance persisted in project | Optional future character metadata; active appearance remains instance/editor state. |
| Node sprite binding | Resolved: not included. Root/tip anchoring on a bone covers joint-position use cases while keeping rotation ownership unambiguous. |
| Bone endpoint terminology | Resolved: the endpoint closer to the skeleton root is the root node; the other endpoint is the tip node. |
| Root/tip orientation | Resolved: both anchors use the same root -> tip local axes. Tip anchoring changes only the origin. |
| Rotation units | Resolved: radians in Core and JSON; editor numeric controls may display degrees. |
| Core package ownership | Resolved and already implemented for current project data: Core is authoritative for `.stickman` ZIP serialization/deserialization and format validation. Artwork/image resources extend that implementation. |
| Atlas pixel format | Resolved: RGBA8; atlas pages are alpha-preserving PNGs with transparent unused space. |
| Atlas rotation | Resolved: never rotate sprites during atlas-page packing. |
| Atlas gutter | Resolved: reserve a one-pixel transparent border around each sprite; page metadata rectangles exclude the gutter. |
| Vendored persistence dependencies | Current code already vendors the JSON parser and `miniz`; add `stb_image`, `stb_image_write`, and `stb_rect_pack` as private Core implementation dependencies. |
| Project root object | Resolved and implemented: `sm::project` owns topology and characters. It does **not** own artwork atlases or appearances as independent project resources. |
| Topology ownership | Resolved and implemented: `sm::topology` owns skeleton/node/bone topology; characters own semantic rig membership above it. Artwork is not topology. |
| Character -> artwork relationship | Resolved: every `sm::character` directly owns its `sm::artwork` member. Artwork lifetime is character lifetime. |
| Artwork -> atlas relationship | Resolved: each character's artwork directly owns one logical `sm::sprite_atlas`. There is no project-level atlas reference. |
| Appearance -> atlas relationship | Resolved: appearances do not store an atlas ID. Every appearance uses the atlas of its owning character's artwork. |
| Cross-character sprite-sheet sharing | Resolved: not supported. Reuse across characters is by explicit copy/import, producing destination-owned resources. |
| Appearance item -> sprite relationship | Resolved: appearance items store a string slot name plus an opaque `sprite_id`, not a sprite name. The sprite ID must resolve within the owning character's atlas. |
| Sprite atlas terminology | Resolved: use `sm::sprite_atlas` for the logical collection because it may span multiple packed bitmaps. Use atlas page for each actual packed bitmap. |

# 15. Intended User Workflow

1. Create/open a `.stickman` project and build one or more skeletons.
2. Promote the relevant loose skeleton(s) into a character, or select an existing character.
3. Open the Artwork Browser; it displays the selected character's directly owned artwork.
4. Create an Empty Appearance or an Appearance from Folder. The appearance is added to that character; it does not create or reference a project-owned atlas.
5. Add/rename logical sprites as needed. All sprites live in that character's atlas, and sprite IDs remain stable when names change.
6. Assign semantic sprite slots to bones in the character's rig.
7. Drag a sprite from the character's atlas onto a bone in the same character.
8. The appearance item records the bone slot string and concrete sprite ID; resource resolution remains inside the character's artwork.
9. Choose whether the item is anchored at the bone root or tip.
10. Set or adjust the item's explicit draw order so overlapping pieces conceal joints correctly.
11. Enter Sprite Transform mode.
12. Translate / rotate / scale the sprite relative to its selected bone endpoint.
13. Repeat for the rest of the character; joint-cover sprites bind to an incident bone and use the appropriate root or tip anchor.
14. Switch active appearances to author alternate artwork against the same rig slots and shared character-local sprite atlas.
15. Switch to another character and verify that its appearances and atlas are independent; there is no cross-character sprite-sheet sharing.
16. Pose or animate the character and verify that sprites follow the correct rig component, endpoint, and orientation.
17. Save the self-contained `.stickman` project through Core; each character's artwork is serialized as part of that character, while atlas packing, PNG encoding, page metadata, and ZIP writing remain invisible to the user.
