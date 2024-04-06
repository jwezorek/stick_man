#include "sm_fabrik.h"
#include "sm_skeleton.h"
#include "sm_visit.h"
#include <unordered_set>
#include <stack>
#include <numbers>

namespace r = std::ranges;
namespace rv = std::ranges::views;

/*------------------------------------------------------------------------------------------------*/

namespace {

	constexpr int k_max_iter = 100;
	constexpr double k_tolerance = 0.005;

	struct bone_info {
		double length;
		double rotation;
	};

	// The core of the implementation of the FABRIK ik algorithm is a DFS over the bones in the
	// skeleton. At any step in the algorithm the current bone along with its predecessor in the
	// dfs traversal may be the neighborhood of one or more rotational constraints. We include the
	// the root node in the following because if there is no prev bone we still need to be able
	// to recover the "current node" which in this case will be the root node.

	struct fabrik_neighborhood {
		sm::node& start_node;
		sm::maybe_bone_ref prev;
		sm::bone& current_bone;
	};

	// return the node that is shared between the current
	// bone and its preceding bone.

	sm::node& current_node(const fabrik_neighborhood& fn) {

		if (!fn.prev) {
			return fn.start_node;
		}
		sm::bone& pred_bone = fn.prev->get();
		auto shared = fn.current_bone.shared_node(pred_bone);
		if (!shared) {
			throw std::runtime_error("invalid fabrik item");
		}
		return shared->get();
	}

	// if the current bone has no predecessor bone, return nil.
	// otherwise, return the node of the predecessor bone that is not 
	// the current node i.e. the node that preceded the current node.

	sm::maybe_node_ref pred_node(const fabrik_neighborhood& fn) {
		if (!fn.prev) {
			return {};
		}
		auto& pred_bone = fn.prev->get();
		return pred_bone.opposite_node( current_node(fn) );
	}

	std::unordered_map<sm::bone*, bone_info> build_bone_table(sm::node& j) {
		std::unordered_map<sm::bone*, bone_info> length_tbl;
		auto visit_bone = [&length_tbl](sm::bone& b)->sm::visit_result {
			length_tbl[&b] = { b.scaled_length(), b.world_rotation() };
			return sm::visit_result::continue_traversal;
			};
		sm::visit_nodes_and_bones(j, {}, visit_bone);
		return length_tbl;
	}

	struct targeted_node {
		sm::node_ref node;
		sm::point target_pos;
		std::optional<sm::point> prev_pos;

		targeted_node(sm::node_ref j, const sm::point& pt) :
			node(j), target_pos(pt), prev_pos()
		{}
	};

	std::vector<targeted_node> pinned_nodes(const std::vector<sm::node_ref>& pins) {
		return pins | rv::transform(
			[](sm::node_ref node)->targeted_node {
				return targeted_node(node, node.get().world_pos());
			}
		) | r::to<std::vector<targeted_node>>();
	}


	sm::point point_on_line_at_distance(const sm::point& u, const sm::point& v, double d) {
		auto pt = sm::point{ u.x + d, u.y };
		return sm::transform(pt, rotate_about_point_matrix(u, angle_from_u_to_v(u, v)));
	}

	std::optional<sm::angle_range> get_forw_rel_rot_constraint(const fabrik_neighborhood& fi) {
		// a forward relative rotation constraint is the normal case. The bone
		// has a rotation constraint on it that is relative to its parent and the
		// predecessor bone is its parent.

		if (!fi.prev) {
			return {};
		}

		const auto& pred_bone = fi.prev->get();
		const auto& curr = fi.current_bone;
		auto curr_constraint = curr.rotation_constraint();

		if (curr_constraint) {
			if (curr.parent_bone() && &curr.parent_bone()->get() != &pred_bone) {
				return {};
			}

			auto curr_pos = current_node(fi).world_pos();
			auto pred_pos = pred_node(fi)->get().world_pos();
			auto anchor_angle = angle_from_u_to_v(pred_pos, curr_pos);

			return sm::angle_range{
				sm::normalize_angle(curr_constraint->start_angle + anchor_angle),
				curr_constraint->span_angle
			};
		}

		return {};
	}

	std::optional<sm::angle_range> get_back_rel_rot_constraint(const fabrik_neighborhood& fi) {
		// a backward relative constraint occurs when the predecessor bone
		// has a relative-to-parent rotation constraint and the current bone
		// is the predecessor's parent.

		if (!fi.prev) {
			return {};
		}

		const auto& pred_bone = fi.prev->get();
		const auto& curr = fi.current_bone;
		auto pred_constraint = pred_bone.rotation_constraint();

		if (!pred_constraint || pred_constraint->relative_to_parent == false) {
			return {};
		}

		if (&pred_bone.parent_bone()->get() != &curr) {
			return {};
		}

		auto curr_pos = current_node(fi).world_pos();
		auto pred_pos = pred_node(fi)->get().world_pos();
		auto anchor_angle = angle_from_u_to_v(pred_pos, curr_pos);
		auto start_angle = -(pred_constraint->start_angle + pred_constraint->span_angle);

		return sm::angle_range{
			sm::normalize_angle(start_angle + anchor_angle),
			pred_constraint->span_angle
		};
	}

	std::optional<sm::angle_range>  get_relative_rot_constraint(const fabrik_neighborhood& fi) {

		auto forward = get_forw_rel_rot_constraint(fi);
		if (forward) {
			return forward;
		}
		return get_back_rel_rot_constraint(fi);

	}

	sm::angle_range absolute_constraint(bool is_forward, double start_angle, double span_angle) {
		return sm::angle_range{
			is_forward ? start_angle : sm::normalize_angle(start_angle + std::numbers::pi),
				span_angle
		};
	}

	std::optional<sm::angle_range> get_absolute_rot_constraint(const fabrik_neighborhood& fi) {
		const sm::bone& curr = fi.current_bone;
		auto constraint = curr.rotation_constraint();

		if (!constraint) {
			return {};
		}

		if (constraint->relative_to_parent) {
			return {};
		}

		const auto& pivot_node = current_node(fi);
		return absolute_constraint(
			(&pivot_node == &curr.parent_node()), constraint->start_angle, constraint->span_angle
		);
	}

	std::vector<sm::angle_range> get_applicable_rot_constraints(const fabrik_neighborhood& fi) {
		std::vector<sm::angle_range> constraints;
		auto absolute = get_absolute_rot_constraint(fi);

		if (absolute) {
			constraints.push_back(*absolute);
		}

		auto relative = get_relative_rot_constraint(fi);
		if (relative) {
			constraints.push_back(*relative);
		}

		return constraints;
	}

	std::vector<sm::angle_range> intersect_angle_ranges(const std::vector<sm::angle_range>& ranges) {

		if (ranges.size() > 2) {
			throw std::runtime_error("invalid rotational constraints");
		}

		if (ranges.size() == 2) {
			return sm::intersect_angle_ranges(ranges[0], ranges[1]);
		}

		return { ranges.front() };
	}

	double constrain_angle_to_ranges(double theta, std::span<sm::angle_range> ranges) {

		// If theta is in one of the angle ranges there is nothing to do...
		for (const auto& range : ranges) {
			if (sm::angle_in_range(theta, range)) {
				return theta;
			}
		}

		std::vector<double> angles;
		angles.reserve(2 * ranges.size());

		for (const auto& range : ranges) {
			angles.push_back(range.start_angle);
			angles.push_back(sm::normalize_angle(range.start_angle + range.span_angle));
		}

		double closest_dist = std::numeric_limits<double>::max();
		double closest = 0.0;

		for (auto angle : angles) {
			auto dist = std::abs(sm::angular_distance(theta, angle));
			if (dist < closest_dist) {
				closest_dist = dist;
				closest = angle;
			}
		}

		return closest;
	}

	double constrain_angle_to_range(double theta, sm::angle_range range) {
		return constrain_angle_to_ranges(theta, { &range,1 });
	}

	std::optional<double> apply_rotation_constraints(const fabrik_neighborhood& fi, double theta) {

		auto constraints = get_applicable_rot_constraints(fi);
		if (constraints.empty()) {
			return {};
		}

		auto intersection_of_ranges = intersect_angle_ranges(constraints);
		if (intersection_of_ranges.empty()) {
			// if the constraints can not all be satisfied, default to the first one...
			intersection_of_ranges = { constraints.front() };
		}

		return constrain_angle_to_ranges(theta, intersection_of_ranges);
	}

	sm::point apply_rotation_constraints(const fabrik_neighborhood& fi, const sm::point& free_pt) {

		auto pivot_pt = current_node(fi).world_pos();
		auto old_theta = angle_from_u_to_v(pivot_pt, free_pt);
		auto new_theta = apply_rotation_constraints(fi, old_theta);

		if (!new_theta) {
			return free_pt;
		}

		return sm::transform(
			sm::point{ sm::distance(pivot_pt, free_pt), 0.0 },
			translation_matrix(pivot_pt) * sm::rotation_matrix(*new_theta)
		);

	}

	sm::point constrain_angular_velocity(
			const fabrik_neighborhood& fi, double original_rot, double max_angle_delta,
			const sm::point& free_pt) {

		auto curr = fi.current_bone;
		const auto& pivot_node = current_node(fi);
		auto old_theta = angle_from_u_to_v(pivot_node.world_pos(), free_pt);
		bool is_forward = (&pivot_node == &curr.parent_node());

		auto start_angle = sm::normalize_angle(original_rot - max_angle_delta);
		auto new_theta = constrain_angle_to_range(
			old_theta,
			absolute_constraint(is_forward, start_angle, 2.0 * max_angle_delta)
		);

		return sm::transform(
			sm::point{ sm::distance(pivot_node.world_pos(), free_pt), 0.0 },
			translation_matrix(pivot_node.world_pos()) * sm::rotation_matrix(new_theta)
		);

	}

	sm::point apply_all_constraints(
			const sm::point& curr_pos,
			const fabrik_neighborhood& neighborhood,
			bool apply_rot_constaints,
			double max_ang_delta,
			double old_bone_rotation ) {

		sm::point new_pos = curr_pos;
		if (apply_rot_constaints) {
			new_pos = apply_rotation_constraints(neighborhood, new_pos);
		}

		if (max_ang_delta > 0.0) {
			new_pos = constrain_angular_velocity(
				neighborhood,
				old_bone_rotation,
				max_ang_delta,
				new_pos
			);
		}

		return new_pos;
	}

	void perform_one_fabrik_pass(sm::node& start_node, const sm::point& target_pt,
		const std::unordered_map<sm::bone*, bone_info>& bone_tbl, bool use_constraints,
		double max_ang_delta) {

		auto perform_fabrik_on_bone = 
			[&](sm::maybe_bone_ref prev, sm::bone& current_bone)->sm::visit_result {

			fabrik_neighborhood neighborhood{ start_node, prev, current_bone };
			auto& leader_node = current_node(neighborhood);
			auto& follower_node = current_bone.opposite_node(leader_node);

			auto new_follower_pos = point_on_line_at_distance(
				leader_node.world_pos(),
				follower_node.world_pos(),
				bone_tbl.at(&current_bone).length
			);

			new_follower_pos = apply_all_constraints(
				new_follower_pos,
				neighborhood,
				use_constraints,
				max_ang_delta,
				bone_tbl.at(&current_bone).rotation
			);
			
			follower_node.set_world_pos(new_follower_pos);
			return sm::visit_result::continue_traversal;
		};

		start_node.set_world_pos(target_pt);
		sm::visit_bone_hierarchy(start_node, perform_fabrik_on_bone);
	}

	sm::result target_satisfaction_state(const targeted_node& tj, double tolerance) {

		// has it reached the target?
		if (sm::distance(tj.node.get().world_pos(), tj.target_pos) < tolerance) {
			return sm::result::fabrik_target_reached;
		}

		// has it moved since the last iteration?
		if (sm::distance(tj.node.get().world_pos(), tj.prev_pos.value()) < tolerance) {
			return sm::result::fabrik_converged;
		}

		return sm::result::fabrik_no_solution_found;
	}

	bool is_satisfied(const targeted_node& tj, double tolerance) {
		auto result = target_satisfaction_state(tj, tolerance);
		return result == sm::result::fabrik_target_reached ||
			result == sm::result::fabrik_converged;
	}

	bool found_ik_solution(std::span<targeted_node> targeted_nodes, double tolerance) {
		auto unsatisfied = r::find_if(targeted_nodes,
			[tolerance](const auto& tj)->bool {
				return !is_satisfied(tj, tolerance);
			}
		);
		return (unsatisfied == targeted_nodes.end());
	}

	void  update_prev_positions(std::vector<targeted_node>& targeted_nodes) {
		for (auto& tj : targeted_nodes) {
			tj.prev_pos = tj.node.get().world_pos();
		}
	}

	void solve_for_multiple_targets(std::span<targeted_node> targeted_nodes,
		const std::unordered_map<sm::bone*, bone_info>& bone_tbl,
		const sm::fabrik_options& opts, bool use_constraints) {
		int j = 0;
		do {
			if (++j > opts.max_iterations) {
				return;
			}
			for (auto& pinned_node : targeted_nodes) {
				perform_one_fabrik_pass(
					pinned_node.node, pinned_node.target_pos, bone_tbl, use_constraints,
					opts.max_ang_delta
				);
			}
		} while (!found_ik_solution(targeted_nodes, opts.tolerance));
	}
}

sm::fabrik_options::fabrik_options() :
	max_iterations{ k_max_iter },
	tolerance{ k_tolerance },
	forw_reaching_constraints{ false },
	max_ang_delta{ 0.0 }
{}

sm::result sm::perform_fabrik(
	const std::vector<std::tuple<node_ref, point>>& effectors,
	const std::vector<sm::node_ref>& pins,
	const fabrik_options& opts) {

	auto bone_tbl = build_bone_table(std::get<0>(effectors.front()));
	auto targeted_nodes = pinned_nodes(pins);
	auto num_pinned_nodes = targeted_nodes.size();

	r::copy(
		effectors |
		rv::transform(
			[](const auto& tup)->targeted_node {
				const auto& [node, pt] = tup;
				return { node, pt };
			}
		),
		std::back_inserter(targeted_nodes)
	);

	auto pinned_nodes = std::span{
		targeted_nodes.begin(),
		targeted_nodes.begin() + num_pinned_nodes
	};

	auto effectors_and_targets = std::span{
		targeted_nodes.begin() + num_pinned_nodes,
		targeted_nodes.end()
	};

	auto has_pinned_nodes = !pinned_nodes.empty();
	int iter = 0;

	do {
		if (++iter >= opts.max_iterations) {
			return result::fabrik_no_solution_found;
		}
		update_prev_positions(targeted_nodes);

		// reach for targets from effectors...
		solve_for_multiple_targets(effectors_and_targets, bone_tbl, opts, !has_pinned_nodes);

		// reach for pinned locations from pinned nodes
		if (has_pinned_nodes) {
			solve_for_multiple_targets(pinned_nodes, bone_tbl, opts, true);
		}
	} while (!found_ik_solution(targeted_nodes, opts.tolerance));

	return result::fabrik_target_reached; //TODO
	//return (target_satisfaction_state(targeted_nodes, opts.tolerance) == result::fabrik_target_reached) ?
	//	result::fabrik_target_reached :
	//	result::fabrik_converged;
}

double sm::constrain_rotation(sm::bone& b, double theta) {

	fabrik_neighborhood fi{
		b.parent_node(),
		b.parent_bone(),
		b
	};
	return ::apply_rotation_constraints(fi, theta).value_or(theta);
}

sm::point sm::apply_rotation_constraints(
		const sm::point& curr_pos, 
		sm::node& start_node, 
		sm::maybe_bone_ref prev, 
		sm::bone& current_bone, 
		bool apply_rot_constaints, 
		double max_ang_delta, 
		double old_bone_rotation) {

	fabrik_neighborhood neighborhood{ start_node, prev, current_bone };
	return apply_all_constraints(
		curr_pos, 
		neighborhood, 
		apply_rot_constaints,
		max_ang_delta, 
		old_bone_rotation
	);
}
