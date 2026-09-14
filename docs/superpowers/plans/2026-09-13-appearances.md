# Appearances Phase 1 Implementation Plan

Goal: implement docs/Appearances.md Phase 1 and early runtime packed resources on appearances, without commits.
Architecture: character-owned validated artwork, immutable shared image storage with region views, memory-only stb codecs and rectangle packing, private ZIP resource helpers, undoable Qt browser.
Tech stack: C++23, bundled stb/miniz, nlohmann JSON, Qt Widgets.
Spec: docs/Appearances.md.

1. Add tests/appearances.cpp and CMake test target. Verify a saved character lacks artwork before implementation.
2. Add sm_image.hpp/cpp: decode(span<byte>), from_rgba, region, row, encode_png. Bound image sizes and expose top-down straight-alpha RGBA8 rows without mutable backing access.
3. Add sm_artwork.hpp/cpp: private maps of frames, slots, appearances; validated semantic mutations; runtime pack(page_size, padding) returns PNG pages, dimensions, and named frame rectangles/origins. Use stb_rect_pack without rotation and extrude padding.
4. Add private sm_artwork_io and sm_package helpers. Save generated pages under character IDs; load shared page regions and reject duplicate names, invalid rectangles, missing pages, unknown references and nonfinite transforms atomically. Preserve version 4 empty-artwork files.
5. Add character artwork access and membership snapshots so topology undo restores resources. Add mdl::project::edit_artwork to execute validated snapshots through undo/redo.
6. Add character-scoped Qt Artwork Browser: active appearance, thumbnails, bulk memory-buffer import, frame origin/name/deletion, slot/state/mapping editors. Resolve selected character or member topology; disable for loose/mixed selection. Preserve active appearance as session state.
7. Extend tests for semantic validation, image alpha/regions, multipage packing/extrusion, malformed package atomicity, and editor undo/context. Build application and all test targets, run CTest, inspect diff and leave source uncommitted.

## Completion

All seven steps are implemented. New saves use version 5; version 4 remains readable.
The application and all test targets build with MSVC. CTest reports 32/32 passing.
The offscreen Artwork Browser render was inspected. Independent review findings
about clipboard resource preservation, bone remapping, large-frame padding limits,
and thumbnail regeneration were addressed. Changes remain uncommitted on appearances.
