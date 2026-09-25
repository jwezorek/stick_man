#include <cmath>
#include "sm_fabrik.hpp"
#include "sm_constraint_geometry.hpp"
#include "sm_geometry_batch.hpp"
#include "sm_skeleton.hpp"
#include "sm_visit.hpp"
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
				return targeted_node(node, node->world_pos());
			}
		) | r::to<std::vector<targeted_node>>();
	}


	sm::point point_on_line_at_distance(const sm::point& u, const sm::point& v, double d) {
		auto pt = sm::point{ u.x + d, u.y };
		return sm::transform(pt, rotate_about_point_matrix(u, angle_from_u_to_v(u, v)));
	}

 struct constraint_failure { sm::result code; };
 sm::point apply_all_constraints(const sm::point& proposed,const fabrik_neighborhood& fn,bool use,double max_delta,double old_rotation,const sm::constraint_geometry& geometry) {
  if(geometry.status()!=sm::result::success)throw constraint_failure{geometry.status()};
  auto& leader=current_node(fn);bool forward=&leader==&fn.current_bone.parent_node();
  double theta=forward?sm::angle_from_u_to_v(leader.world_pos(),proposed):sm::angle_from_u_to_v(proposed,leader.world_pos());
  auto allowed=geometry.allowed_angles(fn.current_bone,use);
  if(max_delta>0)allowed=allowed.intersect(sm::angle_set({old_rotation-max_delta,2*max_delta}));
  auto clamped=allowed.closest_angle(theta);if(!clamped)throw constraint_failure{sm::result::unsatisfiable_constraints};
  double direction=*clamped+(forward?0:std::numbers::pi),length=sm::distance(leader.world_pos(),proposed);
  return {leader.world_x()+length*std::cos(direction),leader.world_y()+length*std::sin(direction)};
 }

	void perform_one_fabrik_pass(sm::node& start_node, const sm::point& target_pt,
		const std::unordered_map<sm::bone*, bone_info>& bone_tbl, bool use_constraints,
		double max_ang_delta, sm::constraint_geometry& geometry,const std::unordered_map<sm::node*,sm::point>& pins) {
        std::unordered_set<size_t> projected;
        std::unordered_map<sm::bone*,double> rotations;
        for(auto [b,info]:bone_tbl)rotations[b]=info.rotation;
        auto perform_fabrik_on_bone = 
			[&](sm::maybe_bone_ref prev, sm::bone& current_bone)->sm::visit_result {
			// The table contains only this effector region, including its boundary bones.
			if (!bone_tbl.contains(&current_bone)) return sm::visit_result::terminate_branch;

			fabrik_neighborhood neighborhood{ start_node, prev, current_bone };
			auto& leader_node = current_node(neighborhood);
			auto& follower_node = current_bone.opposite_node(leader_node);

			auto new_follower_pos = point_on_line_at_distance(
				leader_node.world_pos(),
				follower_node.world_pos(),
				bone_tbl.at(&current_bone).length
			);

            if(auto fan=geometry.fan_for(&current_bone)) {
                if(projected.insert(*fan).second) {
                    auto outcome=geometry.project_fan(*fan,current_bone,leader_node,new_follower_pos,use_constraints,pins,max_ang_delta,rotations);
                    if(outcome!=sm::result::success)throw constraint_failure{outcome};
                }
                return sm::visit_result::continue_traversal;
            }
            new_follower_pos = apply_all_constraints(
				new_follower_pos,
				neighborhood,
				use_constraints,
				max_ang_delta,
				bone_tbl.at(&current_bone).rotation, geometry
			);
			
			follower_node.set_world_pos(new_follower_pos);
			return sm::visit_result::continue_traversal;
		};

		start_node.set_world_pos(target_pt);
		sm::visit_bone_hierarchy(start_node, perform_fabrik_on_bone);
	}

	sm::result target_satisfaction_state(const targeted_node& tj, double tolerance) {

		// has it reached the target?
		if (sm::distance(tj.node->world_pos(), tj.target_pos) < tolerance) {
			return sm::result::fabrik_target_reached;
		}

		// has it moved since the last iteration?
		if (sm::distance(tj.node->world_pos(), tj.prev_pos.value()) < tolerance) {
			return sm::result::fabrik_converged;
		}

		return sm::result::fabrik_no_solution_found;
	}

	bool is_satisfied(const targeted_node& tj, double tolerance) {
		auto result = target_satisfaction_state(tj, tolerance);
		return result == sm::result::fabrik_target_reached ||
			result == sm::result::fabrik_converged;
	}

	bool all_targets_settled(std::span<targeted_node> targeted_nodes, double tolerance) {
		auto unsatisfied = r::find_if(targeted_nodes,
			[tolerance](const auto& tj)->bool {
				return !is_satisfied(tj, tolerance);
			}
		);
		return (unsatisfied == targeted_nodes.end());
	}

	sm::result fabrik_result(std::span<targeted_node> targeted_nodes, double tolerance) {
		bool target_reached = false;
		bool converged = false;

		for (const auto& targeted_node : targeted_nodes) {
			switch (target_satisfaction_state(targeted_node, tolerance)) {
			case sm::result::fabrik_target_reached:
				target_reached = true;
				break;

			case sm::result::fabrik_converged:
				converged = true;
				break;

			default:
				return sm::result::fabrik_no_solution_found;
			}
		}

		if (target_reached && converged) {
			return sm::result::fabrik_mixed;
		}
		return target_reached ? sm::result::fabrik_target_reached : sm::result::fabrik_converged;
	}

	void  update_prev_positions(std::vector<targeted_node>& targeted_nodes) {
		for (auto& tj : targeted_nodes) {
			tj.prev_pos = tj.node->world_pos();
		}
	}

	void solve_for_multiple_targets(std::span<targeted_node> targeted_nodes,
		const std::unordered_map<sm::bone*, bone_info>& bone_tbl,
		const sm::fabrik_options& opts, bool use_constraints, sm::constraint_geometry& geometry,const std::unordered_map<sm::node*,sm::point>& pins) {
		int j = 0;
		do {
			if (++j > opts.max_iterations) {
				return;
			}
			for (auto& pinned_node : targeted_nodes) {
				perform_one_fabrik_pass(
					pinned_node.node, pinned_node.target_pos, bone_tbl, use_constraints,
					opts.max_ang_delta, geometry, pins
				);
			}
		} while (!all_targets_settled(targeted_nodes, opts.tolerance));
	}

	sm::result validate_fabrik_inputs(
		const std::vector<std::tuple<sm::node_ref, sm::point>>& effectors,
		const std::vector<sm::node_ref>& pins) {

		if (effectors.empty()) {
			return sm::result::fabrik_no_solution_found;
		}

		auto& owner = std::get<0>(effectors.front())->owner();
		for (const auto& effector : effectors) {
			auto node = std::get<0>(effector);
			if (&node->owner() != &owner) {
				return sm::result::cross_skeleton_bone;
			}
		}

		for (const auto& node : pins) {
			if (&node->owner() != &owner) {
				return sm::result::cross_skeleton_bone;
			}
		}

		return sm::result::success;
	}
}

sm::fabrik_options::fabrik_options() :
	max_iterations{ k_max_iter },
	tolerance{ k_tolerance },
	forw_reaching_constraints{ false },
	max_ang_delta{ 0.0 }
{}

static sm::result solve_fabrik_region(
	const std::vector<std::tuple<sm::node_ref, sm::point>>& effectors,
	const std::vector<sm::node_ref>& pins,
	const sm::fabrik_options& opts,
	const std::unordered_set<sm::bone*>& bones) {
	using sm::result;

	auto bone_tbl = build_bone_table(std::get<0>(effectors.front()));
	std::erase_if(bone_tbl, [&](const auto& entry) { return !bones.contains(entry.first); });
	auto targeted_nodes = pinned_nodes(pins);
	auto num_pinned_nodes = targeted_nodes.size();
    sm::constraint_geometry geometry(std::get<0>(effectors.front())->owner().owner());
    if(geometry.status()!=result::success)return geometry.status();
    std::unordered_map<sm::node*,sm::point> fixed;
    for(auto pin:pins)fixed[pin.ptr()]=pin->world_pos();
    struct pose_transaction {
        std::unordered_map<sm::node*,sm::point> saved; bool committed=false;
        ~pose_transaction(){if(!committed)for(auto [n,pt]:saved)n->set_world_pos(pt);}
    } transaction;
    for(auto b:bones){transaction.saved.try_emplace(&b->parent_node(),b->parent_node().world_pos());transaction.saved.try_emplace(&b->child_node(),b->child_node().world_pos());}

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
		solve_for_multiple_targets(
			effectors_and_targets,
			bone_tbl,
			opts,
			!has_pinned_nodes || opts.forw_reaching_constraints, geometry, fixed
		);

		// reach for pinned locations from pinned nodes
		if (has_pinned_nodes) {
			solve_for_multiple_targets(pinned_nodes, bone_tbl, opts, true, geometry, fixed);
		}
	} while (!all_targets_settled(targeted_nodes, opts.tolerance));

    for(auto [n,pt]:fixed)if(sm::distance(n->world_pos(),pt)>opts.tolerance)return result::unsatisfiable_constraints;
    for(auto [n,pt]:fixed)n->set_world_pos(pt);
    for(auto [b,info]:bone_tbl)if(std::abs(b->scaled_length()-info.length)>opts.tolerance)return result::unsatisfiable_constraints;
    auto valid=geometry.validate(opts.tolerance,true,&bones);if(valid!=result::success)return valid;
    transaction.committed=true;
    return fabrik_result(targeted_nodes, opts.tolerance);
}

sm::result sm::perform_fabrik(
	const std::vector<std::tuple<node_ref, point>>& effectors,
	const std::vector<node_ref>& pins,
	const fabrik_options& opts) {
	const auto validation = validate_fabrik_inputs(effectors, pins);
	if (validation != result::success) return validation;
    geometry_batch batch(std::get<0>(effectors.front())->owner().owner());
	std::unordered_set<node*> boundaries;
	for (auto pin : pins) boundaries.insert(pin.ptr());
	// A pinned effector cannot be moved to a different target.
	for (auto [effector, target] : effectors) {
		if (boundaries.contains(effector.ptr()) && distance(effector->world_pos(), target) > opts.tolerance)
			return result::fabrik_no_solution_found;
	}

	struct region {
		std::unordered_set<node*> nodes;
		std::unordered_set<bone*> bones;
		std::vector<node_ref> pins;
		std::vector<std::tuple<node_ref, point>> effectors;
	};
	std::vector<region> regions;
	std::unordered_set<node*> assigned;
	// Discover all regions before changing geometry. Pins belong to the
	// boundary of each adjacent region, but do not connect those regions.
	for (auto [effector, target] : effectors) {
		if (boundaries.contains(effector.ptr()) || assigned.contains(effector.ptr())) continue;
		auto& component = regions.emplace_back();
        sm::constraint_geometry geometry(effector->owner().owner());
        if(geometry.status()!=result::success)return geometry.status();
        std::vector<std::pair<node*,bone*>> pending{{effector.ptr(),nullptr}};
        std::unordered_set<node*> found_pins;
        while(!pending.empty()) {
            auto [n,incoming]=pending.back();pending.pop_back();
            if(boundaries.contains(n)) {
                if(found_pins.insert(n).second)component.pins.push_back(*n);
                if(incoming)if(auto fan=geometry.fan_for(incoming))for(auto member:geometry.fan_members(*fan)) {
                    if(&member->parent_node()==n&&component.bones.insert(member).second)pending.push_back({&member->child_node(),member});
                }
                continue;
            }
            if(!component.nodes.insert(n).second)continue;
            assigned.insert(n);
            for(auto edge:n->adjacent_bones())if(component.bones.insert(edge.ptr()).second)pending.push_back({&edge->opposite_node(*n),edge.ptr()});
        }

		for (auto entry : effectors)
			if (component.nodes.contains(std::get<0>(entry).ptr())) component.effectors.push_back(entry);
	}

	bool reached = false, converged = false, failed = false;
	for (const auto& component : regions) {
		sm::result outcome;
        try {outcome=solve_fabrik_region(component.effectors, component.pins, opts, component.bones);}
        catch(const constraint_failure& failure){return failure.code;}
        if(outcome==result::invalid_constraint||outcome==result::inconsistent_constraints||outcome==result::unsatisfiable_constraints)return outcome;
		reached |= outcome == result::fabrik_target_reached || outcome == result::fabrik_mixed;
		converged |= outcome == result::fabrik_converged || outcome == result::fabrik_mixed;
		failed |= outcome == result::fabrik_no_solution_found;
	}
	if (failed) return result::fabrik_no_solution_found;
    if (auto status = batch.commit(); status != result::success) return status;
	if (reached && converged) return result::fabrik_mixed;
	return converged ? result::fabrik_converged : result::fabrik_target_reached;
}

sm::result sm::perform_fabrik(
		node_ref effector,
		point effector_target,
		std::optional<sm::node_ref> pin,
		const fabrik_options& opts) {

	std::vector<std::tuple<sm::node_ref, sm::point>> one_effector = {
		{effector, effector_target}
	};

	std::vector<sm::node_ref> pinned;
	if (pin) {
		pinned.push_back(*pin);
	}

	return sm::perform_fabrik(one_effector, pinned, opts);
}

double sm::constrain_rotation(sm::bone& b, double theta) {
    constraint_geometry geometry(b.owner().owner());
    auto clamped = geometry.allowed_angles(b).closest_angle(theta);
    if (!clamped) throw std::invalid_argument("unsatisfiable rotation constraints");
    return *clamped;
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
		old_bone_rotation, constraint_geometry(current_bone.owner().owner())
	);
}

