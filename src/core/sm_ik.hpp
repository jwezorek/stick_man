#pragma once

#include "sm_types.hpp"

namespace sm {

    struct ik_options {
        // Compatibility surface retained for callers. The SLSQP backend maps each
        // unit to a small fixed evaluation budget; it is no longer an algorithm pass count.
        int max_iterations;

        // Positional target tolerance in the application's existing coordinate units.
        double tolerance;

        // Compatibility no-op. The replacement solver always enforces angular
        // constraints in both directions/phases.
        bool forw_reaching_constraints;

        // When finite and in (0, pi), bounds each affected bone's total world-angle
        // change from the incoming pose for this invocation. Other values disable it.
        double max_ang_delta;

        ik_options();
    };

    // Public names are retained for compatibility; the implementation uses reduced
    // 2D angle coordinates and NLopt SLSQP. Pins delimit independent movement regions.
    result perform_ik(
        const std::vector<std::tuple<node_ref, point>>& effectors,
        const std::vector<sm::node_ref>& pinned_nodes,
        const ik_options& opts = {}
    );

    result perform_ik(
        node_ref effector,
        point effector_target,
        std::optional<sm::node_ref> pin,
        const ik_options& opts = {}
    );

    double constrain_rotation(sm::bone& b, double theta);

    sm::point apply_rotation_constraints(
        const sm::point& curr_pos,
        sm::node& start_node,
        sm::maybe_bone_ref prev,
        sm::bone& current_bone,
        bool apply_rot_constaints = true,
        double max_ang_delta = 0.0,
        double old_bone_rotation = 0.0);
}
