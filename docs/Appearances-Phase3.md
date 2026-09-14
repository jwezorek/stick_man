# Appearances Phase 3

The Structure tab authors the character-wide state vocabulary. Creating, renaming
or deleting a state updates the shared slot definition; rename/delete propagates
through every appearance's mappings as one undoable edit. The mandatory `default`
state cannot be renamed or deleted.

In Appearances, select a state child to edit its image mapping. Choose a frame,
Hidden (none), or Use default (unmapped). An unmapped state uses that appearance's
default image, whereas Hidden draws nothing. The default mapping itself cannot
be unmapped.

Select a slot or any of its state children and use **Preview slot state** to
preview a declared semantic state. Each slot keeps its own preview choice, and
switching appearances preserves those choices. The tree shows each slot's active
preview state in brackets. Editing a mapping does not implicitly change which
state is being previewed.

**Reset all preview states** returns the current character's slots to default.
Preview choices are session state: they do not edit the project, add undo entries,
or get saved. Renaming/deleting a previewed state or slot resets its preview to
default. Opening a project clears all preview choices.

Rendering, sprite hit-testing, selection outlines and transform previews all use
the resolved preview image. Hidden states cannot be clicked on the canvas; their
slot remains available in the browser. Core's memory-only resource and semantic
resolution APIs are unchanged. Animation timeline integration remains Phase 4.

The `selection_character_artwork_phase3` regression covers explicit frame and
hidden mappings, fallback across two appearances, hit-testing, live mapping edits,
undo, reset, rename/delete propagation and session-state clearing on reopen.
