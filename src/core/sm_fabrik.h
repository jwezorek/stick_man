#pragma once

#include "sm_types.h"

/*------------------------------------------------------------------------------------------------*/

namespace sm {

	struct fabrik_options {
		int max_iterations;
		double tolerance;
		bool forw_reaching_constraints;
		double max_ang_delta;

		fabrik_options();
	};

	result perform_fabrik(
		const std::vector<std::tuple<node_ref, point>>& effectors,
		const std::vector<sm::node_ref>& pinned_nodes,
		const fabrik_options& opts = {}
	);

	result perform_fabrik(
		node_ref effector,
		point effector_target,
		std::optional<sm::node_ref> pin,
		const fabrik_options& opts = {}
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