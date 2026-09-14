# Appearances Phase 2

Spec: docs/Appearances.md Phase 2. Preserve the user's Structure / Appearances / Images browser layout. Work on appearances without commits.

1. Core: add project::resolve_artwork returning ordered resolved_sprite values (slot, frame, image, registration_origin, transform, bone_transform). transform maps image-centered Cartesian coordinates into world coordinates with origin subtraction; bone_transform maps bone-local coordinates into world. Default/missing/hidden mappings and unresolved bones follow spec. Add math and semantic tests.
2. Canvas: add artwork_layer, owned by scene after model initialization. Render logical frames below skeleton guides; retain active appearances per character and independent sprite selection. Hit-test front to back. Add Show Artwork / Show Skeleton / wireframe controls. Sprite Transform tool supports translate, rotate and X/Y scale, preview in editor state and one undoable command on release; Escape/tool changes cancel.
3. Browser: preserve tabs, show implemented slots in painter order and absent definitions separately in tree, support reorder buttons and top-level drag reorder, numeric transform controls and canvas-selection sync. Export frame drag MIME with character ID/frame name; canvas bone drop chooses existing visual channel or creates one compound undoable slot/mapping edit.
4. Tests: baseline existing suite, Core transform cases, Qt rendering orientation/order, preview controls, sprite transform with rig unchanged and undo/redo, frame drop, browser ordering, unresolved bindings. Independent review and offscreen visual inspection; all edits remain uncommitted.

Integration contracts live in src/ui/canvas/artwork_layer.hpp. Core exposes project::resolve_artwork(id, appearance, states={}) returning std::vector<resolved_sprite>.

Completed all four tasks. Application and regression targets build; all 33 CTest
tests pass. Offscreen rendering was visually inspected. Independent review found
a canvas notification ordering issue; it was fixed and covered by deletion/reopen
regressions. Changes remain uncommitted on appearances.
