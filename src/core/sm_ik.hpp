#pragma once

#include "sm_types.hpp"
#include "sm_constraint.hpp"

namespace sm {

    // The implementation uses reduced 2D angle coordinates and NLopt SLSQP.
    // Pins delimit independent movement regions.
    result perform_ik(
        const std::vector<std::tuple<node_ref, point>>& effectors,
        const std::vector<sm::node_ref>& pinned_nodes
    );

    result perform_ik(
        const std::vector<std::tuple<node_ref, point>>& effectors,
        const std::vector<sm::node_ref>& pinned_nodes,
        const constraint_map& constraints
    );

    result perform_ik(
        node_ref effector,
        point effector_target,
        std::optional<sm::node_ref> pin
    );

    result perform_ik(
        node_ref effector,
        point effector_target,
        std::optional<sm::node_ref> pin,
        const constraint_map& constraints
    );

    sm::point apply_rotation_constraints(
        const sm::point& curr_pos,
        sm::node& start_node,
        sm::maybe_bone_ref prev,
        sm::bone& current_bone,
        bool apply_rot_constaints = true,
        double max_ang_delta = 0.0,
        double old_bone_rotation = 0.0);

    sm::point apply_rotation_constraints(
        const sm::point& curr_pos,
        sm::node& start_node,
        sm::maybe_bone_ref prev,
        sm::bone& current_bone,
        const constraint_map& constraints,
        bool apply_rot_constaints = true,
        double max_ang_delta = 0.0,
        double old_bone_rotation = 0.0);
}
