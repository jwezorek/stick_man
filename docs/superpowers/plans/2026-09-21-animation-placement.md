# Visible animation composition

Goal: the editor places an inserted or edited action at the nearest valid position at or above the requested layer. Existing actions retain their order. No runtime graph or implicit reordering remains.

Contract: animated reference frames must follow all other actions whose potential node writes include either reference-bone endpoint. Self contribution and frozen Animation Root are exempt. Timing does not remove dependencies. Same-layer chronological order counts. Reject impossible placement atomically. IK writes stop at pins; rigid rotation conservatively includes all traversed nodes except its pivot because constraints are reapplied throughout the skeleton.

Implementation sequence:
- Add regression coverage for visible evaluation order, placement, scopes, and invalid arrangements.
- Replace runtime dependency sorting with ordinary layer order; add topology-aware order validation and pure placement helpers.
- Route timeline creation, edits, and dragging through placement, including reference-change probes.
- Update documentation and tests, build, run regressions, and review the complete change.

Existing saved animations with invalid visible order are rejected by topology-aware validation; there is no hidden migration or runtime reordering.

Completed: core and UI implementation, runtime graph removal, documentation, regressions, and independent review. Review identified an early UI overlap rejection; removed it and added an undo/atomic-rejection UI regression. Corrected a stale skeleton-wrapper reference in the existing animation fixture so that suite can run. Full Debug build succeeds. Full suite: 40/43 passed, with the three previously established failures in character_stage4, appearances, and rotation_editor.
