# First-class constraints: Core and solver design

Status: proposed for review. No implementation or commits yet.

## Scope and intended outcome

Implement the supplied constraint redesign on the current branch. Nodes and bones remain topology; project-owned constraints restrict configurations. Preserve the existing absolute/parent rotation editor, including undo and visual feedback. Add programmatic arbitrary-bone rotation references and rigid triangles, with no new editor tools or panes. Do not migrate old project JSON. Do not commit changes.

## Current source and integration boundaries

`sm_bone.hpp` owns an optional `rot_constraint`. `sm_skeleton.cpp` serializes it inside bones and copies it with topology. `sm_project` maintains the global ID index and governs topology replacement. Its archive currently writes version 6 project JSON and accepts older versions.

`sm_fabrik.cpp` derives absolute and forward/backward parent constraints separately, allows at most two applicable ranges, and silently selects the first range on empty intersection. Its pin regions stop at every pin. A cleanup guard restores individual pin coordinates, potentially violating rigid geometry.

`bone::rotate_by`, bone/node property editing, `rig_interaction.cpp`, animation pose application/evaluation, and undo reconstruction all write geometry outside FABRIK. Model transform commands snapshot the old bone-owned constraint. Topology snapshots alone will therefore be insufficient after this refactor.

## Approach

Use project-owned constraint records plus a shared transient angular/geometry compilation layer below FABRIK. This preserves the existing tree solver while making exact angular relations reusable by other geometry operations.

Keeping a constraint store in topology would simplify scratch copies but would conflate topology and project semantics. A general iterative physics solver would be broader than required and would not meet the exact-per-pass fan requirement. Neither alternative is selected.

## Core objects and ownership

Add `sm_constraint.hpp/.cpp` with named, persistently identified rotation and rigid-triangle objects. Expose immutable query views; project methods alone change their definitions. Rotation references are a tagged world/parent/bone representation, with an object ID only for the bone case. Rotation records contain target ID and `angle_range`. Triangle records contain two bone IDs and a signed, normalized rest delta.

Remove the optional constraint and its mutation/query API from `bone`. Extend const project-object lookup and naming to constraints, preserving the existing restriction against mutable aggregate lookup. Include constraints in global ID uniqueness checks, clear, integrity validation, and staged deserialization.

Provide project add/update/remove/query operations returning the existing expected/result conventions. Support multiple rotation constraints per bone. `constraints_for_bone` includes both target and reference participation; a separate target-rotation query serves the editor. Deterministic ordering uses persistent IDs, never unordered-map iteration.

Reject invalid/nonfinite ranges, unresolved IDs, self-reference, parent references without a parent, duplicate unordered triangle pairs, identical triangle arms, and arms without the same root node. Arbitrary references may address another skeleton in the same project: an out-of-region reference provides its current world orientation during that solve; it does not implicitly make another skeleton movable.

Programmatic triangle creation captures the current signed relative angle. Restoration/deserialization accepts a stored delta and validates it without recapturing the current pose. Exact cycle validation compares accumulated offsets modulo a full turn with a documented numerical tolerance. Longer redundant cycles are valid; duplicate pairs remain invalid.

## Circular angle sets and angular relations

Introduce `angle_set`, represented by a normalized union of closed intervals over one turn. Explicitly represent empty and full sets, retain singleton angles, and split wrap-around ranges at the seam. Provide intersection, rotation/translation, negation, containment, and nearest-angle queries. Nearest angle on an empty set returns failure, never an arbitrary angle. Handle equivalent seam endpoints and deterministic nearest-endpoint ties.

Compile rotation references into endpoint relations `target - reference in allowed`, where world is a fixed zero endpoint and parent resolves to a bone ID. Negation changes `[start, start + span]` into `[-start - span, -start]`, modulo one turn. The same relation is queried from either endpoint; traversal direction only converts a directed segment angle into the bone's canonical parent-to-child orientation.

All applicable ranges, including angular-velocity limits when enabled, intersect through `angle_set`. Distinguish invalid constraint definitions, inconsistent exact cycles, empty angular intersections, and iterative target nonconvergence in result handling. FABRIK remains iterative: an empty feasible set at a projection is an explicit solve failure, not a proof that every globally rearranged pose is impossible.

## Rigid fan compilation

Build a graph of exact triangle relations for each pivot. Connected components become transient fans containing member bones, current lengths, and signed offsets from a deterministic reference member. DFS/BFS assigns offsets and checks every back edge for agreement modulo a turn. No fan IDs, memberships, or caches are persisted on bones.

Reduce angular constraints to component degrees of freedom. A target member contributes `reference_angle + allowed - target_offset`. A relation observed from its reference member contributes the inverted set. If both endpoints belong to the same fan, their offsets cancel the component angle: validate the fixed difference directly rather than clamping against a stale orientation. Multiple member contributions are intersected before projecting the fan once.

## FABRIK integration and pins

Compile constraints once per solve against the active geometry. Ordinary bones retain the existing length projection. Each pass tracks projected fans. The first reached member drives a fan: with the pivot as leader, derive orientation from its proposed tip; with a tip as leader, derive orientation and pivot from that member's fixed length. Clamp the one fan orientation against the combined angular set, then position all tips using their stored offsets and lengths. Later members skip independent projection while traversal continues into their descendants.

Hard fan projection runs in both halves of FABRIK regardless of the option that disables angular limits on forward reaching. Preserve direction-independent stored relations throughout.

Discover regions using bone connectivity with fan links across a pinned pivot. Only members of the same fan connect through that boundary; unrelated branches remain separate. Multiple effectors in a merged region solve together. A pinned pivot fixes fan translation. Pinned tips restrict its remaining freedom; incompatible fixed positions return failure.

Replace individual pin-coordinate cleanup with transactional pose handling. Capture the entry pose and restore the complete pose on constraint failure or failure to retain pins and hard geometry. Never report target success solely because targets have stopped moving: validate pins, lengths, exact fan deltas, and enabled rotation relations before returning a successful/converged result.

## Geometry outside FABRIK

Place fan projection and relation evaluation in a shared Core geometry layer. Project-owned topology receives access to its constraint context; standalone scratch/evaluation geometry receives an explicit immutable constraint snapshot resolved against its own IDs. Do not use pointers into live geometry while evaluating scratch poses.

Public geometry changes operate as validated transactions; raw coordinate assignment is reserved for internal batch reconstruction. Direct rotation, `rotate_by`, rigid animation, and ragdoll traversal project all affected fan members together and carry their descendant geometry. A rotation of one arm necessarily rotates its rigid siblings even for the existing single-bone rotation mode.

Length edits explicitly change that arm's length while retaining signed fan angles and moving its descendants. A constrained tip-position edit becomes a constrained geometry request rather than an unchecked coordinate write. Batch pose/undo restoration validates at the end, avoiding projection after each intermediate node write. Reject incompatible geometric transforms or poses atomically instead of exposing a broken fan. Translation, rotation, and uniform scaling preserve the signed angular invariant; reflection or nonuniform scaling requires validation and may fail.

Audit every `set_world_pos`, `apply`, `rotate_by`, and hierarchy reconstruction call site, including `sm_animation.cpp`, `sm_animation_evaluate.cpp`, model commands, node/bone properties, and `rig_interaction.cpp`.

## Lifecycle, copying, and history

Topology replacement computes affected constraint changes before mutation. Remove records referencing deleted bones. Revalidate parent references and sibling relationships after split/merge; remove those made invalid deterministically. Preserve surviving IDs and rest angles. Include constraint removals/restoration in command snapshots so undo restores identical constraint identities.

Copy operations carry an explicit semantic constraint snapshot alongside scratch topology. Duplicate constraint IDs and remap every copied bone endpoint. Include target-owned rotation constraints and triangles whose two arms are copied. Same-project duplication may retain a valid external rotation reference; cross-project clipboard export omits relationships whose required endpoint is outside the copied selection, so paste cannot leave unresolved IDs. Return/report omitted relationships through the copy result where applicable. Internal references always remap; never silently bind an external reference to a coincidentally matching destination ID.

Extend ID collision checks and regeneration to constraint IDs. Preflight the complete copy/replacement before publishing any mutation.

## Persistence

Bump the project JSON version and accept only the new schema. Require an explicit project-level `constraints` array, including an empty array when appropriate. Remove bone-owned rotation serialization and reject its old representation.

Rotation entries contain `type`, `id`, `name`, `target_bone`, tagged `reference`, `start_angle`, and `span_angle`. Triangle entries contain `type: rigid_triangle`, `id`, `name`, `bone1`, `bone2`, and `relative_angle`. Use the existing object ID encoding. Load topology first into staging, then resolve and validate all constraints, including exact cycles and global ID collisions, before replacing the live project. Preserve IDs and signed rest geometry on round trip.

## Existing rotation editor

Change constraint tool, bone property controls, canvas adornments, and interaction queries to project lookups. The UI selects the deterministic first world/parent rotation constraint targeting the bone, edits that object's ID, creates a new object if none exists, and removes only the selected UI-supported constraint. Arbitrary-reference constraints stay headless and are not repurposed by these controls.

Use model commands that snapshot complete constraint records rather than `old_bone_to_rotcon`. Add/remove/update and undo/redo preserve record IDs and names. Maintain current parent-availability behavior and existing angular visual feedback.

## Verification and acceptance

Add focused Core tests for reference kinds, inverse traversal, wrap-around/full/singleton sets, intersections of three or more ranges, and explicit empty-set failure. Test sibling validation, duplicate pairs, either driving arm, signed handedness, shared-arm and larger fans, consistent redundant cycles, and inconsistent restored cycles.

Exercise dragging every triangle node, articulated parents and descendants at multiple tips, pinned pivots/tips/remote nodes, unrelated pinned branches, multiple effectors, and combined member limits. Assert hard geometry after individual passes as well as complete solves. Test failures leave the entry pose intact.

Test direct rotations, single-bone rotations, length edits, node property moves, rigid animation, ragdoll reconstruction, scratch poses, and history restoration. Cover JSON identity round trips, arbitrary references, malformed records, deletion/split cleanup, duplication, clipboard remapping, and undo/redo.

Build Core, model, and UI targets. Run the new tests and existing CTest regressions, especially `fabrik_pins`, `rotation_editor`, topology/selection clipboard regressions, animation rotation, and animation referential integrity. Report any environment limitations explicitly.

## Expected behavior changes and limits

- Old saved project JSON is unsupported.
- Empty applicable angular intersections return failure instead of choosing the first range.
- Editing one rigid arm can move its sibling arms and descendants.
- Invalid hard-geometry edits fail atomically.
- Arbitrary references outside a solve region act as current-orientation anchors; this stage does not introduce a project-wide multi-skeleton solver.
- FABRIK remains a local iterative solver and does not guarantee a solution for every feasible cyclic set of ranged relations.
- No new constraint editor UI, triangle topology, or generic physics framework is introduced.
