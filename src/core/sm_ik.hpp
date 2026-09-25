#pragma once

#include "sm_types.hpp"

namespace sm {

    // The implementation uses reduced 2D angle coordinates and NLopt SLSQP.
    // Pins delimit independent movement regions.
    result perform_ik(
        const std::vector<std::tuple<node_ref, point>>& effectors,
        const std::vector<sm::node_ref>& pinned_nodes
    );

    result perform_ik(
        node_ref effector,
        point effector_target,
        std::optional<sm::node_ref> pin
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
