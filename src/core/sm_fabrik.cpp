#include "sm_fabrik.h"
#include "sm_skeleton.h"
#include "sm_traverse.h"
#include <unordered_set>
#include <stack>
#include <numbers>

namespace r = std::ranges;
namespace rv = std::ranges::views;

namespace {

	constexpr int k_max_iter = 100;
	constexpr double k_tolerance = 0.005;

	struct bone_info {
		double length;
		double rotation;
	};

	// The core of the implementation of the FABRIK ik algorithm is a DFS over the bones in the
	// skeleton. At any step in the algorithm the current bone along with its predecessor in the
	// dfs traversal may be the neighborhood of one or more rotational constraints. This
	// fabrik item class models such a neighborhood.

	class fabrik_item {
		sm::node_or_bone<sm::node, sm::bone> pred_;
		sm::bone_ref current_;
		sm::node* current_node_;
		mutable sm::node* pred_node_;

	public:

		fabrik_item(sm::node_or_bone<sm::node, sm::bone> p, sm::bone_ref b) :
			pred_{ p },
			current_{ b },
			current_node_(nullptr),
			pred_node_(nullptr) {
		}

		fabrik_item(sm::bone& b) :
			pred_{
				(b.parent_bone()) ?
					sm::node_or_bone<sm::node, sm::bone>{*b.parent_bone()} :
					sm::node_or_bone<sm::node, sm::bone>{b.parent_node()}
		},
			current_{ b },
			current_node_(nullptr),
			pred_node_(nullptr) {
		}

		sm::bone& current_bone() {
			return current_.get();
		}

		const sm::bone& current_bone() const {
			return current_.get();
		}

		// return the node that is shared between the current
		// bone and its preceding bone.

		sm::node& current_node() {
			if (!current_node_) {
				if (std::holds_alternative<sm::node_ref>(pred_)) {
					current_node_ = &std::get<sm::node_ref>(pred_).get();
				}
				else {
					sm::bone& pred_bone = std::get<sm::bone_ref>(pred_);
					auto shared = current_.get().shared_node(pred_bone);
					if (!shared) {
						throw std::runtime_error("invalid fabrik item");
					}
					current_node_ = &shared->get();
				}
			}
			return *current_node_;
		}

		const sm::node& current_node() const {
			auto* nonconst_self = const_cast<fabrik_item*>(this);
			return nonconst_self->current_node();
		}

		// if the current bone has a node as its predecessor, return nil.
		// otherwise, return the node of the predecessor bone that is not 
		// the current node i.e. the node that preceded the current node.

		sm::maybe_node_ref pred_node() const {
			if (!pred_node_ && std::holds_alternative<sm::bone_ref>(pred_)) {
				auto& pred_bone = std::get<sm::bone_ref>(pred_).get();
				pred_node_ = &pred_bone.opposite_node(current_node());
			}
			if (!pred_node_) {
				return {};
			}
			return *pred_node_;
		}

		sm::maybe_const_bone_ref pred_bone() const {
			return (std::holds_alternative<sm::node_ref>(pred_)) ?
				std::optional<sm::bone_ref>{} :
				std::get<sm::bone_ref>(pred_);
		}

		// the trick here is to just return the parent and children of the current bone, 
		// not sibling bones unless this is the root. this forces a traversal in which it is 
		// impossible for an applicable rotation constraint to be relative to a bone that is 
		// not the predecessor bone.(because we do not currently support sibling constraints)

		std::vector<sm::bone_ref> neighbor_bones(const std::unordered_set<sm::bone*>& visited) {
			auto neighbors = current_bone().child_bones();
			auto parent = current_bone().parent_bone();
			if (parent) {
				neighbors.push_back(*parent);
			}
			else {
				r::copy(current_bone().sibling_bones(), std::back_inserter(neighbors));
			}
			return neighbors |
				rv::filter(
					[&visited](auto neighbor) {
						return !visited.contains(&neighbor.get());
					}
			) | r::to<std::vector<sm::bone_ref>>();
		}
	};

	using fabrik_item_visitor = std::function<bool(fabrik_item&)>;

	auto to_fabrik_items(auto prev, std::span< sm::bone_ref> bones) {
		return bones | rv::transform([prev](auto b) {return fabrik_item(prev, b); });
	}

	void fabrik_traversal(sm::node& start, fabrik_item_visitor visitor) {
		std::stack<fabrik_item> stack;
		std::unordered_set<sm::bone*> visited;
		auto adj_bones = start.adjacent_bones();
		stack.push_range(
			to_fabrik_items(sm::node_ref(start), adj_bones)
		);
		while (!stack.empty()) {
			auto item = stack.top();
			stack.pop();
			if (!visitor(item)) {
				return;
			}
			visited.insert(&item.current_bone());
			auto neighbors = item.neighbor_bones(visited);
			stack.push_range(
				to_fabrik_items(std::ref(item.current_bone()), neighbors)
			);
		}
	}

	std::unordered_map<sm::bone*, bone_info> build_bone_table(sm::node& j) {
		std::unordered_map<sm::bone*, bone_info> length_tbl;
		auto visit_bone = [&length_tbl](sm::bone& b)->sm::visit_result {
			length_tbl[&b] = { b.scaled_length(), b.world_rotation() };
			return sm::visit_result::continue_traversal;
			};
		sm::dfs(j, {}, visit_bone);
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

	std::optional<sm::angle_range> get_forw_rel_rot_constraint(const fabrik_item& fi) {
		// a forward relative rotation constraint is the normal case. The bone
		// has a rotation constraint on it that is relative to its parent and the
		// predecessor bone is its parent.

		if (!fi.pred_bone()) {
			return {};
		}

		const auto& pred_bone = fi.pred_bone()->get();
		const auto& curr = fi.current_bone();
		auto curr_constraint = curr.rotation_constraint();

		if (curr_constraint) {
			if (curr.parent_bone() && &curr.parent_bone()->get() != &pred_bone) {
				return {};
			}

			auto curr_pos = fi.current_node().world_pos();
			auto pred_pos = fi.pred_node()->get().world_pos();
			auto anchor_angle = angle_from_u_to_v(pred_pos, curr_pos);

			return sm::angle_range{
				sm::normalize_angle(curr_constraint->start_angle + anchor_angle),
				curr_constraint->span_angle
			};
		}

		return {};
	}

	std::optional<sm::angle_range> get_back_rel_rot_constraint(const fabrik_item& fi) {
		// a backward relative constraint occurs when the predecessor bone
		// has a relative-to-parent rotation constraint and the current bone
		// is the predecessor's parent.

		if (!fi.pred_bone()) {
			return {};
		}

		const auto& pred_bone = fi.pred_bone()->get();
		const auto& curr = fi.current_bone();
		auto pred_constraint = pred_bone.rotation_constraint();

		if (!pred_constraint || pred_constraint->relative_to_parent == false) {
			return {};
		}

		if (&pred_bone.parent_bone()->get() != &curr) {
			return {};
		}

		auto curr_pos = fi.current_node().world_pos();
		auto pred_pos = fi.pred_node()->get().world_pos();
		auto anchor_angle = angle_from_u_to_v(pred_pos, curr_pos);
		auto start_angle = -(pred_constraint->start_angle + pred_constraint->span_angle);

		return sm::angle_range{
			sm::normalize_angle(start_angle + anchor_angle),
			pred_constraint->span_angle
		};
	}

	std::optional<sm::angle_range>  get_relative_rot_constraint(const fabrik_item& fi) {

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

	std::optional<sm::angle_range> get_absolute_rot_constraint(const fabrik_item& fi) {
		const sm::bone& curr = fi.current_bone();
		auto constraint = curr.rotation_constraint();

		if (!constraint) {
			return {};
		}

		if (constraint->relative_to_parent) {
			return {};
		}

		const auto& pivot_node = fi.current_node();
		return absolute_constraint(
			(&pivot_node == &curr.parent_node()), constraint->start_angle, constraint->span_angle
		);
	}


	std::vector<sm::angle_range> get_applicable_rot_constraints(const fabrik_item& fi) {
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

	std::optional<double> apply_rotation_constraints(const fabrik_item& fi, double theta) {

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

	sm::point apply_rotation_constraints(const fabrik_item& fi, const sm::point& free_pt) {

		auto pivot_pt = fi.current_node().world_pos();
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
		const fabrik_item& fi, double original_rot, double max_angle_delta,
		const sm::point& free_pt) {
		auto curr = fi.current_bone();
		const auto& pivot_node = fi.current_node();
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

	void perform_one_fabrik_pass(sm::node& start_node, const sm::point& target_pt,
		const std::unordered_map<sm::bone*, bone_info>& bone_tbl, bool use_constraints,
		double max_ang_delta) {

		auto perform_fabrik_on_bone = [&](fabrik_item& fi) {

			sm::bone& current_bone = fi.current_bone();
			auto& leader_node = fi.current_node();
			auto& follower_node = current_bone.opposite_node(leader_node);

			auto new_follower_pos = point_on_line_at_distance(
				leader_node.world_pos(),
				follower_node.world_pos(),
				bone_tbl.at(&current_bone).length
			);

			if (use_constraints) {
				new_follower_pos = apply_rotation_constraints(fi, new_follower_pos);
			}

			if (max_ang_delta > 0.0) {
				new_follower_pos = constrain_angular_velocity(
					fi, bone_tbl.at(&current_bone).rotation, max_ang_delta, new_follower_pos
				);
			}

			follower_node.set_world_pos(new_follower_pos);
			return true;
			};

		start_node.set_world_pos(target_pt);
		fabrik_traversal(start_node, perform_fabrik_on_bone);
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

	fabrik_item fi(b);
	return ::apply_rotation_constraints(fi, theta).value_or(theta);
}