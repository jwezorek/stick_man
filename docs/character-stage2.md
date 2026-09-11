# Character Refactor Stage 2

Baseline: `character_refactor`, commit `3c2d10d102bbfea3f9216fd9f8bdd4def8461e8b`.
Authoritative specification: `docs/character.md`.

## Core and model APIs

- `sm::project::can_create_bone` preflights endpoint ownership, cross-character membership, cycles and root constraints. Both `create_bone` overloads enforce those checks. Character/loose connections adopt into the existing character in either endpoint order. Cross-character connections return `different_characters` without mutation.
- `plan_replacement` returns inferred membership and `deleted_character_ids`, allowing a later UI to ask for confirmation before executing a destructive edit. It does not mutate the project.
- `replace_skeletons` determines provenance from original node IDs before ID regeneration. Components containing topology from different characters are rejected. Loose source nodes may join one character. When an owned replacement has no surviving node IDs, the caller must supply explicit membership state; there is no fallback to an arbitrary affected character.
- Replacement stages copies and ID remapping before erasure, detaches old memberships without deleting character objects, installs the replacement components, and then prunes empty characters. Direct `delete_skeleton` detaches membership and removes the character only when its rig becomes empty.
- `snapshot_membership` stores affected skeleton IDs mapped to optional character IDs, plus character ID/name metadata. Structural undo/redo passes this semantic state alongside topology-only snapshots. Surviving rig components outside the affected set remain untouched. Destroyed characters are reconstructed with the same ID/name; redo retains the actual inserted topology IDs.
- `adopt_skeletons` validates the whole request before changing membership. It rejects foreign, duplicate or already-owned skeletons. Both core adoption and model adoption preserve topology objects and IDs. Model adoption uses `restore_membership` for undo/redo without recreating topology.
- Model `add_bone`, `replace_skeletons`, adoption and `redo` return ordinary `sm::result` outcomes. Failed commands do not enter the undo stack or discard redo history.
- `has_consistent_membership` checks both directions, live character identity, duplicate rig entries and nonempty rigs. Debug assertions check completed membership mutations.

## Live mutation audit

| Entry point | Membership handling |
| --- | --- |
| Project bone creation; model add-bone command | Core preflight/enforcement, both rig and parent updated after merge; undo restores asymmetric ownership. |
| Project replacement; model replacement auxiliary/command | Provenance checked before remapping; staged topology; deferred character pruning; explicit undo/redo semantics. |
| Clipboard cut/delete and selection splitting | Algorithms build scratch components, then call the model replacement boundary. Algorithms do not know about characters. |
| Project skeleton deletion; create-node undo | Semantic detach and final-component pruning. |
| Project skeleton creation/copy and model paste/duplication | New topology is loose. Copy/duplicate never transfer character references. |
| Core adoption and membership restoration | Validate before mutation and maintain both membership directions. |
| Project clear/deserialization | Whole topology is destroyed/replaced before characters are cleared. Packaged character serialization remains deferred as in Stage 1. |
| Scratch topology public structural methods | Live topology is exposed only as const. Scratch `create_bone` now rejects endpoints owned by another topology, closing the mutable-node bypass. Node/bone structural setters and topology ownership access remain protected/const. |

No supported live split, merge, replacement or skeleton-deletion entry point bypasses project membership maintenance. Stage 1 `remove_character` remains its existing low-level membership removal operation; it is not wired to a whole-character delete UI. No character selection, GUI hierarchy, confirmation dialog, artwork, animation or serialization feature was added.

## Verification

Built `stick_man`, `character_stage1`, `character_stage2`, `topology_regressions` and `selection_regressions` using the existing x64-Debug MSVC/Qt configuration. Run the complete suite with:

```text
cmake --build out/build/x64-Debug --target stick_man character_stage1 character_stage2 topology_regressions selection_regressions
ctest --test-dir out/build/x64-Debug --output-on-failure
```

The non-GUI Stage 2 executable covers loose/owned/same-character merges, both endpoint orders, rejection atomicity and history, split identity, mixed provenance, component/final deletion, character name/identity restoration, adoption validation and undo/redo, remapped boundary nodes, explicit provenance, scratch copy semantics, and repeated invariant/ID checks. Existing selection and topology regressions remain enabled.

Final result: all five build targets succeeded; all 19 CTest tests passed. The existing compiler warnings about ignored nodiscard results in create-node/clipboard code are unchanged.
