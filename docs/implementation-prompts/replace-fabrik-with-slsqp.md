# Implementation prompt: replace FABRIK with reduced 2D angle-space SLSQP

Implement this change in the existing stick_man repository. Assume the current branch is already prepared for replacing FABRIK outright. This is an implementation request: inspect the code, make a short implementation plan, implement the replacement, and verify it. Do not stop after proposing a design. Follow repository instructions and preserve unrelated work.

The required outcome is a working, runtime-capable IK implementation using NLopt's SLSQP algorithm and reduced 2D angle coordinates, reached through the existing FABRIK-facing API. Keep the existing callers and public interface intact. There must be one IK implementation after this change, with no FABRIK fallback, solver selector, or runtime choice of algorithms. Existing public names may retain “fabrik” for compatibility.

1. Understand the application and compatibility boundary.

   stick_man is permanently 2D. Skeletons are trees of nodes connected by fixed-length bones. Constraints include world-relative angular limits, angular limits relative to parent or arbitrary reference bones, and rigid groups of sibling bones (“fans”) with fixed signed angular offsets. There can be zero, one, or multiple pinned nodes. Pins fix positions, not automatically orientations.

   Natural motion, continuity, and interactive/runtime performance matter more than exact global optimization. The incoming pose is the available pose reference. Animation can evaluate IK repeatedly with changing targets; the implementation must work in core code without editor state, Python, external processes, or network access at runtime. Adding runtime variables or changing the animation format is outside this task.

   Start by reading the current solver, constraints, geometry batching, callers, and tests. Relevant repository-relative files include:

   - `src/core/sm_fabrik.hpp` and `sm_fabrik.cpp`
   - `src/core/sm_constraint.hpp`, `sm_constraint_geometry.hpp/.cpp`, and `sm_angle_set.hpp/.cpp`
   - `src/core/sm_geometry_batch.hpp/.cpp`, `sm_bone.cpp`, and `sm_types.hpp`
   - `src/core/sm_animation_evaluate.cpp` and `src/ui/tools/rig_interaction.cpp`
   - `tests/fabrik_pins.cpp`, `tests/constraint_solver.cpp`, and relevant animation/direct-edit tests
   - Root and test CMake files and existing build/dependency instructions

   Treat these as starting points; verify the actual checkout. Do not preserve accidental FABRIK deadlocks or exact historical poses as required behavior.

2. Preserve the public API and caller behavior.

   Retain both `sm::perform_fabrik` overloads, their signatures and defaults, the existing `fabrik_options` fields, and existing result enumerators. Existing code must compile without caller changes. Private implementation files/types are welcome. Do not expose NLopt types in public headers or introduce a public solver framework.

   Preserve `constrain_rotation` and `apply_rotation_constraints`, including their behavior for direct rotation and dragging. These helpers have uses outside the IK solver. Shared constraint utilities may remain; the iterative positional FABRIK solver must be replaced.

   Preserve input validation, including empty effectors, cross-skeleton arguments, and an effector pinned somewhere incompatible with its target. Inspect and handle duplicate inputs and nonfinite inputs deliberately. Preserve defined no-op requests.

   Single-effector actions are the design priority, but the vector overload already supports multiple effectors and has regression coverage. Preserve independent effector regions and jointly targeted fans; do not silently ignore extra effectors or replace them with sequential calls that invalidate earlier targets. Use the same optimization machinery with multiple target residuals where necessary. Do not add a sophisticated task-priority system for multiple effectors.

   Preserve option fields and defaults, and document their new interpretation:

   - `tolerance`: positional target tolerance in the existing coordinate units. Use separately named internal angular and feasibility tolerances.
   - `max_iterations`: controls a finite, deterministic amount of optimization work. Document its mapping to the optimizer's evaluation budget; evaluations are not FABRIK iterations. Share the budget across refinement and any alternative seeds so retries cannot multiply work without a bound.
   - `forw_reaching_constraints`: retained as a documented compatibility no-op. Both values enforce all required angular constraints and produce the same behavior.
   - `max_ang_delta`: when enabled, bounds total world-orientation change of each affected bone from the invocation's incoming pose. It is not a per-iteration allowance and must not reset between phases or seeds. Apply it consistently to all fan members. Preserve existing disabled semantics and define handling of invalid values after inspecting the code.

3. Preserve which geometry may move.

   Pins currently delimit movement regions: ordinary propagation stops at a pin, and geometry beyond that boundary remains unchanged. A participating rigid fan can bring its connected sibling members into the region across their pinned common pivot. Preserve that behavior, including unaffected branches and skeletons.

   Relative-angle constraints must be enforced when either the target bone or the reference bone moves. A reference outside the active region has a frozen orientation; its presence does not authorize moving external geometry. Constraints wholly unrelated to the active region must not reject an otherwise legal solve merely because another part of the project is currently outside its angular limits.

   If otherwise active regions are coupled by a fan or angular relation, account for that coupling in the solve without expanding the authorized movement domain. Truly independent regions must remain independent, including their pose metric and deterministic result.

4. Implement the reduced mathematical model.

   Use independent world-orientation variables and, when needed, a 2D translation. Express node positions through forward kinematics:

       p_v = u + f_v(phi)

   Use one independent orientation per ordinary bone or rigid fan. For a fan member b:

       theta_b = phi_group(b) + fixed_offset_b

   Preserve effective bone lengths and fan offsets from the model, including scaling and signed orientation conventions. Re-rooting numerical traversal must not reverse the meaning of a bone's world angle. Reconstruct all participating branches, not only a path to the effector.

   Lengths and fan relationships must hold by construction, to floating-point accuracy. Do not represent them as soft penalties or stretch bones to reach targets.

   Within a consistent unwrapped angular chart, encode world and relative limits as affine bounds/constraints:

       lower <= theta_b <= upper
       lower <= theta_b - theta_reference <= upper

   Derive these from semantic constraint records. Do not freeze a moving reference at its input orientation by reusing a local clamp as the optimization model. Reuse semantic parsing, fan membership, and validation where appropriate.

   Handle circular ranges correctly: seam-crossing arcs, full-circle ranges, zero-width limits, and intersections that have multiple allowed components. Keep angle lifts consistent with the incoming pose. Do not place discontinuous wrapping, nearest-angle projection, or clamping inside differentiable callbacks. A wrapping seam must not create a false limit. Do not assume all circular intersections form one convex interval or label another angular branch infeasible because one local lift failed. Prefer the current branch; keep any additional branch attempts small, deterministic, and budgeted.

   Eliminate fixed orientations and structurally redundant affine equalities where practical, including zero-width relative limits. Represent exact angular relations as equalities or eliminate them; do not create pairs of opposing inequalities. Detect inconsistent exact relations explicitly. Never replace disjoint allowed angular components with their convex hull.

5. Reduce translation and encode pins carefully.

   For an active region with one effector and no pins, eliminate translation using:

       u = target - f_effector(phi)

   This makes target placement exact for every angle candidate. Optimize the pose preference over the legal angles. Starting from a valid incoming pose, unchanged angles with this translation always supply a valid target-reaching candidate. Optimizer failure must not lose that candidate. Still allow articulation when the pose objective prefers it; do not implement every unpinned solve as unconditional rigid translation.

   With at least one pin a at c_a, eliminate translation using:

       u = c_a - f_a(phi)

   Each additional pin k imposes:

       f_k(phi) - f_a(phi) = c_k - c_a

   For unpinned multiple-effectors, retain translation or eliminate it only when mathematically justified for their combined objective. Do not force the single-effector reduction onto conflicting targets.

   Encode pin equalities as coordinate residuals, not squared-distance-equals-zero constraints whose gradients vanish at satisfaction. Preserve every original pin in final validation. Handle duplicate pins, fully determined poses, zero free variables, and overdetermined/redundant constraints on rigid fans. Do not blindly pass an unusable equality system to SLSQP.

   Remove only established structural redundancies. When several pins constrain a known rigid group, use their geometry to reduce or fix its pose and validate every remaining pin. Jacobian rank loss at a straight-chain singularity is not proof that a nonlinear pin equation can be discarded. Do not silently drop constraints or classify a legal fixed pose as inconsistent merely because the optimizer encounters a singular matrix. Preserve a valid incumbent and report numerical failure separately from proven contradictory constraints through the available result categories.

6. Make target accuracy and pose preference explicit.

   Use the incoming pose captured once per call as the reference. Do not add hidden cross-call state, a new public pose-reference argument, or a fabricated rest pose. Stable numerical ordering and starting from the input pose should support continuous caller-driven updates and deterministic animation evaluation.

   For pinned regions, prefer a bounded two-stage solve:

   - Minimize normalized squared target residuals subject to the hard constraints.
   - Improve pose preference while preserving the best target accuracy found within a small, documented tolerance.

   When a target is already reached, refinement must keep it within the public target tolerance. For multiple targets, preserve per-target success as well as aggregate quality rather than allowing a sum of errors to hide one missed target. For an unreachable target, retain the best legal approximation found.

   Do not encode exact target satisfaction as squared error <= 0. Use coordinate equalities when suitable, or a positive tolerance band. Avoid adding unnecessary equalities that worsen a redundant pin system. Keep a validated pre-refinement candidate if refinement fails.

   A single weighted error-plus-pose objective is acceptable only if a concrete numerical reason makes the staged approach unsuitable and tests demonstrate that pose regularization does not leave avoidable misses beyond the existing target tolerance. Document the tradeoff; merely choosing an enormous target weight is not an adequate priority strategy.

   Start with a simple, scale-normalized pose metric: mean squared Cartesian displacement of active nodes from their incoming positions, plus a small angular-deviation term that preserves articulation. Relative-angle differences are appropriate for parent-child articulation; explain the convention for root bones and fans. Keep weights as a few named private constants. Do not hard-code humanoid bone names or introduce an authoring UI for weights.

   Cartesian displacement prices whole-body translation; an angular-only metric would make pure translation optimal for every valid unpinned single-effector input. Use a characteristic length to normalize distances and a normalized sum so weights do not accidentally depend on units or the number of nodes in an independent region.

   Do not add temporal filtering or velocity penalties without a time parameter. Do not update the pose reference after every optimizer iteration. Small target changes should avoid unnecessary branch flips, body motion, and jitter, but do not claim mathematical continuity for all inputs.

7. Integrate SLSQP for actual runtime use.

   Use NLopt `LD_SLSQP` through its C or C++ API. Integrate NLopt privately with the existing CMake build and current Windows toolchain. Make dependency acquisition/version selection reproducible using the repository's conventions. Ensure static-library consumers receive the required link dependencies without leaking optimizer types into the public API. Keep scripting-language bindings and unrelated tools out of the required build.

   No runtime Python/SciPy, subprocess optimization, runtime downloading, Ipopt/CppAD stack, or custom replacement for SLSQP is needed.

   Supply analytic derivatives for FK, objective terms, and constraints. The elementary derivative is:

       d/dtheta [L*cos(theta), L*sin(theta)]
           = [-L*sin(theta), L*cos(theta)]

   Include fan sharing, translation elimination, and relative-angle coupling in the chain rule. Finite differences are for verification, not the production algorithm. Precompute stable graph/index data for a call and avoid allocation-heavy callbacks. Use deterministic ordering rather than pointer or unordered-container iteration order.

   Scale variables and residuals sensibly. Handle finite evaluation budgets, roundoff-limited exits, unsuccessful line searches, and exceptions. Solver status alone does not establish target attainment or pose validity.

   A perfectly straight pinned limb with an inward target can have zero first-order target gradient despite a legal bending solution. Include a bounded deterministic escape strategy, such as small angle-space alternative seeds consistent with the incoming bend preference. Use the same SLSQP solver for recovery. Angle-legal seeds need not satisfy multiple pins: restore feasibility or reject them before considering them for commit. Do not randomly reseed every frame or search an unbounded number of branches.

8. Keep trial geometry private and commit atomically.

   Evaluate optimization callbacks on scratch coordinates. Public node setters can invoke constrained movement and recursively call IK; do not use them to evaluate trials.

   Validate a candidate before publishing it: finite coordinates, fixed effective lengths, all relevant pins, world/relative limits, fan offsets and handedness, enabled angle-change bounds, and unchanged excluded geometry. Use sufficiently strict internal feasibility tolerances; the visible target tolerance is not permission for appreciable pin drift.

   Use `geometry_batch` correctly for the final coordinate write and commit. Inspect nested-batch behavior: an inner batch may not have its own rollback snapshot. Ensure invocation-level restoration on all rejected writes or failures, including during animation evaluation and calls affecting several regions. If pins are snapped to their exact saved values, revalidate lengths and fans afterward.

   Keep the best independently validated candidate, including the incoming pose when legal. An incoming pose may itself violate newly applied limits, so attempt legal feasibility recovery when appropriate; do not assume it is always a usable fallback.

   Map outcomes into existing result values using actual committed geometry. `fabrik_target_reached` requires all requested targets within tolerance. Preserve meaningful `fabrik_converged`/`fabrik_mixed` aggregation for valid residual solutions. Numerical termination, a stationary pose, and a proven contradictory constraint set are different outcomes. Failure paths must leave the pre-call geometry intact. Document this mapping without adding public result values.

9. Verify behavior through the unchanged API.

   Preserve existing regression coverage. Add focused tests that assert geometric outcomes, not just successful optimizer return codes. Do not weaken tests merely to accommodate the replacement. Update a historical pose expectation only when it specifies incidental solver behavior rather than a required contract, and explain why.

   Cover these cases:

   - Unpinned, angularly constrained humanoid-like tree: hand reaches its target, all limits/fans/lengths remain legal, and translation remains available.
   - Ordinary one-pin reaches and a straight two-bone inward reach requiring a legal bend.
   - Known-reachable constrained chains, including prior FABRIK deadlock examples available in the repository. Where useful, derive targets from saved legal configurations.
   - Reachable and unreachable multi-pin requests; exact pin preservation; fully fixed and multiply pinned fans.
   - Pin barriers, inactive branches, arbitrary external references, and constraints where the moving bone is the reference endpoint.
   - Articulated/nested fans, fixed offsets, and handedness.
   - Circular seam crossings, zero-width/full-circle limits, contradictory limits, and compatible intersections requiring careful angle lifting.
   - Existing vector-overload cases: independent regions and jointly targeted fans.
   - `max_ang_delta` across the entire call, compatibility no-op behavior of `forw_reaching_constraints`, low work budgets, invalid input, and zero-DOF requests.
   - Successful batch commit plus rollback after rejection, including invocation within an existing geometry batch. A returned success must correspond to retained coordinates.
   - Existing animation evaluation and direct-edit paths, demonstrating dynamic targets still use the replacement without caller changes.

   Verify analytic derivatives against finite differences away from wrapping boundaries, including fan variables and pin-based translation elimination.

   Add deterministic short target trajectories to check repeated solves, branch flips, and pose drift. Restrict strict continuity assertions to cases with a comfortably reachable continuous branch; do not demand continuity across genuinely incompatible constraints or discontinuous targets.

   Run the relevant tests and available complete regression suite. Measure representative solve time and evaluation counts, including difficult constrained/multi-pin cases. Report the build configuration and hardware; do not assert an unmeasured frame-time guarantee or put fragile wall-clock thresholds in ordinary tests.

10. Deliver the complete replacement.

    Finish the implementation, dependency integration, tests, and concise documentation. Explain the coordinate reduction, pose objective and weights, angular chart policy, option/result compatibility mapping, and numerical limitations. Remove obsolete FABRIK-only solver machinery while retaining public helpers still used elsewhere.

    Keep scope focused: no animation format redesign, new runtime-variable feature, new UI controls, broad repository refactor, solver plugin architecture, or preservation of FABRIK as an alternative backend.

    In the final report, state what changed, how the unchanged API reaches SLSQP, which checks actually ran and passed, measured performance, and any unresolved limitations. A build or test blocked by the environment must be reported accurately rather than represented as verified.

Implementation references:

- [NLopt SLSQP algorithm documentation](https://nlopt.readthedocs.io/en/latest/NLopt_Algorithms/#slsqp)
- [NLopt C++ API, constraints, and stopping criteria](https://nlopt.readthedocs.io/en/latest/NLopt_C-plus-plus_Reference/)
- [NLopt build and installation documentation](https://nlopt.readthedocs.io/en/latest/NLopt_Installation/)
