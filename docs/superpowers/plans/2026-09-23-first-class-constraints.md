# First-class constraints implementation plan

Execution: implement in this session, as explicitly requested. No commits or branch changes.
Spec: ../specs/2026-09-23-first-class-constraints-design.md

- [ ] Core records and project APIs: immutable constraint views, validated project mutation, global identity, copy/remap and deletion. Add Core tests before implementing the APIs.
- [ ] Circular angular sets: independently test wrapping, singleton/full sets, arbitrary intersections, inversion and empty-set failure in sm_angle_set.
- [ ] Persistence and history: explicit project constraints array, staged validation, exact IDs/rest deltas, semantic snapshots alongside scratch geometry.
- [ ] Compile angular relations and rigid fans: validate exact cycles, reduce member limits, expose reusable geometry projection below FABRIK.
- [ ] FABRIK: driver projection once per fan per pass, fan-aware pinned regions, explicit errors and rollback preserving hard geometry.
- [ ] Other geometry and existing UI: replace bone-owned APIs, preserve add/edit/remove undo, include animation and interaction scratch contexts.
- [ ] Verification: build all targets, run focused and full CTest suites, independently review uncommitted changes and fix material findings.

Review focus: seam singleton ranges; references inside the same fan; reference IDs crossing scratch-copy boundaries; multiple pinned fan members; atomic failure when geometry cannot satisfy limits.

Interface contract: sm::constraint holds immutable ID/name and a variant of rotation_constraint/rigid_triangle_constraint payloads. topology::constraints() supplies a const semantic context for geometry and copying. Only project APIs edit the live context. sm::compiled_constraints resolves this context against geometry; sm::angle_set implements set operations independent of traversal.
