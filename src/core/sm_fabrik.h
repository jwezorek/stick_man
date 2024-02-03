#pragma once

#include "sm_types.h"
#include "sm_traverse.h"

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

	double constrain_rotation(sm::bone& b, double theta);

}