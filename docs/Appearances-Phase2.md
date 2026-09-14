# Appearances Phase 2

The existing Structure / Appearances / Images tabs are preserved. The canvas now
renders each character's active appearance below its skeleton guides. Active
appearance selection remains editor session state.

## Authoring

- Import frames in Images, then drag a frame onto a bone of the same character.
  If the bone already has slots, choose an existing slot or create a new one.
  Creating the slot and assigning its default frame is one undoable edit.
- Use the Appearances controls to create, rebind or delete slots and choose the
  root or tip anchor. Unresolved bindings are marked in the tree and skipped by
  the renderer.
- Implemented slots appear in painter order: top is back, bottom is front.
  Drag a slot row to reorder it, or use Bring Forward, Send Backward, Bring to
  Front and Send to Back. State children stay with their slot.
- Select Sprite Transform and choose Move, Rotate, Scale X, Scale Y or Scale X
  and Y. Drag a sprite to edit its appearance transform while keeping the rig
  fixed. Release creates one undo step; Escape cancels the preview.
- Edit exact translation, rotation in degrees, and X/Y scale values in
  Appearances. Negative scales mirror the sprite.
- View provides Show Artwork, Show Skeleton and Normal/Wireframe skeleton
  display. Hide the skeleton to preview the artwork alone.

## Core and renderer boundary

`project::resolve_artwork(character_id, appearance_name, states)` returns
`resolved_sprite` values in painter order. Each value includes slot and frame
names, an immutable logical image resource, registration origin, the bone-local
to world matrix, and the frame-local to world matrix. Frame-local coordinates
are centered Cartesian coordinates (+Y up); image rows remain top-down RGBA8.

The optional state map selects declared semantic states. Missing selections use
`default`; missing mappings fall back to `default`; explicit hidden targets and
unresolved bones produce no sprite. Editor-selectable state preview is described
in [Phase 3](Appearances-Phase3.md).

Core continues to accept and return memory buffers, with no filesystem or Qt
dependencies. The existing packed sprite-sheet resource API is unchanged. The
editor renders logical images directly, including shared sheet-backed regions.

## Verification

The full CTest suite passes (33 tests). Core tests cover anchors, registration,
translation/rotation/signed scale, pose changes, state resolution and unresolved
bindings. The Phase 2 Qt regression covers pixel orientation, painter order,
transform previews and undo/redo, browser controls, visibility, frame drops,
deselection, character deletion and project reopening. The offscreen editor
capture was also inspected for layout and rendering.
