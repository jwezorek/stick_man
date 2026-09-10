# Character Refactor Stage 2 Implementation Plan

Spec: docs/character.md and the Stage 2 request. Work inline on the clean character_refactor baseline.

Architecture: project owns membership policy. Infer replacement provenance from surviving node IDs before remapping; reject components spanning different characters. Explicit semantic snapshots override inference during restoration, preserving loose/owned asymmetry and character metadata. Scratch topology carries no character references.

- [x] Add non-GUI regressions and observe the missing merge membership behavior.
- [x] Implement core merge validation, adoption, semantic deletion, membership validation, and transactional replacement membership. Add a replacement preflight with character-deletion information.
- [x] Extend command state with affected skeleton ownership and character metadata. Restore snapshots through the replacement boundary; retain inserted IDs for redo. Return expected user failures without adding history.
- [x] Cover mixed replacement provenance, cross-character rejection, splits, final deletion, adoption, repeated undo/redo and unique IDs.
- [x] Audit live mutation entry points, build the app and tests, run CTest, review the diff and create an archive of changed source/test/document files only.

