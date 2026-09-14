# Phase 1 implementation notes

`sm::character` owns its artwork. `project.artwork(character_id)` is the narrow
mutable entry point; generic project lookup still only mutates nodes and bones.
Artwork containers have const accessors. Semantic operations validate names,
references, mandatory defaults and finite transforms, and throw
`std::invalid_argument` for invalid edits (`std::out_of_range` for missing lookups).
The editor stages edits through `mdl::project::edit_artwork` as one undoable command.

## Image and runtime APIs

```cpp
// The host reads a file or downloads bytes; Core only receives memory.
std::vector<std::uint8_t> png_bytes = /* host-provided PNG bytes */;
auto& art = project.artwork(character_id);
art.insert_frame("head", png_bytes);
art.set_registration_origin("head", {2.0, -3.0});

const auto& image = art.frames().at("head").image;
auto first_row = image.row(0); // const span of width * 4 bytes
auto encoded = image.encode_png(); // vector<uint8_t>

auto packed = art.pack();
for (const auto& page : packed.pages) {
    // Host decodes/uploads page.png, or uses image_resource::decode(page.png).
    // page.width and page.height describe the encoded PNG dimensions.
}
for (const auto& frame : packed.frames) {
    // frame.name, frame.page, frame.rect, frame.registration_origin
    // identify the host texture and true frame rectangle, excluding padding.
}
```

Decoded images use straight-alpha RGBA8, with top-down rows. Logical image regions
share immutable backing and keep it alive, including after the source project is
destroyed. `row(y)` abstracts the backing stride; callers must not assume adjacent
logical rows are contiguous. Frame origin `(0, 0)` is the image center, with +Y up.

Decoded image dimensions are limited to 8192; logical frames are limited to 8190
so they always fit with one pixel of padding. Default packing starts at 2048 and
grows to fit larger frames, up to 8192. `pack(page_size, padding)` supports explicit
limits and uses additional pages as necessary. Pages are cropped to occupied
bounds, never rotate frames, extrude all edges/corners into padding, and leave
unoccupied pixels transparent. Packing returns independent PNG buffers; layout
may change between calls and is not a stable frame identity.

`appearance::appearance_slots` is the ordered painter list (the member avoids
Qt's `slots` macro). `resolve_frame` distinguishes an explicitly hidden target
from an absent mapping, which falls back to `default`. `project::slot_resolved`
requires a live bone in the owning character; absent or foreign bones remain
authored references and do not resolve.

## Persistence and editor

New saves use project format **5**. Version 4 files still load with empty artwork.
Each character's JSON artwork contains `pages`, `frames`, `slots`, and `appearances`
arrays. PNG pages live under `characters/<id>/artwork/page-N.png`. Frame rectangles
use top-left pixel coordinates. Duplicate names, unknown references, invalid
rectangles, missing/damaged resources and duplicate JSON keys are rejected before
the live project is replaced. Pages decode once and loaded frames retain shared
regions. Newly imported standalone frames can coexist with those regions.

The Artwork Browser is available in the View menu. Select a character or its
member topology to edit it; loose or mixed-character selections clear the context.
The browser supports bulk import, thumbnails, frame rename/delete and origin
editing, appearance create/rename/delete/switch, slot binding and vocabulary, and
appearance state mappings. A referenced frame cannot be deleted until its mappings
are changed. All artwork edits are undoable. Character copy/paste preserves
independent artwork semantics and remaps bone IDs; topology undo restores artwork.

Canvas sprite rendering, painter-order controls and sprite transform authoring
remain Phase 2 work. Active appearance selection is session state.

## Verification

`appearances` exercises memory codecs, shared regions, semantic mutations and
validation, packing/extrusion/multiple pages, package round trips, malformed load
atomicity, legacy loading and bone-remap preservation. `selection_character_artwork`
exercises browser context, thumbnails, appearance switching, origin/mapping edits,
undo/redo, copy/paste, deletion restoration and reopen. The existing topology,
character and selection CTest suite remains applicable.
