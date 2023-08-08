#include "ik_sandbox.h"
#include "ik_types.h"
#include <cmath>
#include <variant>
#include <stack>
#include <unordered_set>
#include <unordered_map>
#include <limits>
#include <numbers>
#include <qdebug.h>

namespace r = std::ranges;
namespace rv = std::ranges::views;

/*------------------------------------------------------------------------------------------------*/

namespace {

	//TODO: remove debug code
	double radians_to_degrees(double radians) {
		return radians * (180.0 / std::numbers::pi_v<double>);
	}
	//

    using node_or_bone = std::variant<sm::bone_ref, sm::node_ref>;

	struct bone_with_prev {
		node_or_bone prev;
		sm::bone_ref current;

		bone_with_prev(sm::node& n, sm::bone& b) : prev{ n }, current{ b } {
		}

		bone_with_prev(sm::bone_ref p, sm::bone_ref b) : prev{ p }, current{ b } {
		}
	};

	using bone_pair_visitor = std::function<bool(bone_with_prev&)>;


	auto append_predecessor(auto prev, std::span<const sm::bone_ref> bones) {
		return bones | rv::transform([prev](auto&& b) {return bone_with_prev{ prev, b }; });
	}

	// if bwp has a node as the bone's predecessor, return that node.
	// otherwise, return the node that is shared between the current
	// bone and its preceding bone.

	sm::node& current_node(const bone_with_prev& bwp) {
		if (std::holds_alternative<sm::node_ref>(bwp.prev)) {
			return std::get<sm::node_ref>(bwp.prev).get();
		}
		else {
			sm::bone& prev_bone = std::get<sm::bone_ref>(bwp.prev).get();
			auto shared = bwp.current.get().shared_node(prev_bone);
			if (!shared) {
				throw std::runtime_error("bad call to dfs_bones_with_prev");
			}
			return shared->get();
		}
	}

	std::optional<sm::bone_ref> predecessor_bone(const bone_with_prev& bwp) {
		if (std::holds_alternative<sm::node_ref>(bwp.prev)) {
			return {};
		}
		return std::get<sm::bone_ref>(bwp.prev);
	}

	// given a bone B and its predecessor, return the bones that are adjacent
	// to the node that is not shared by B and its predecessor and that
	// are not B.

	std::vector<sm::bone_ref> neighbor_bones(const bone_with_prev& bwp) {
		sm::node& curr_node = current_node(bwp);
		sm::node& next_node = bwp.current.get().opposite_node(curr_node);
		return next_node.adjacent_bones() |
			rv::filter(
				[&bwp](sm::bone_ref b) { return &(b.get()) != &bwp.current.get(); }
			) | r::to<std::vector<sm::bone_ref>>();
	}

	void dfs_bones_with_prev(sm::node& start, bone_pair_visitor visitor) {
		std::stack<bone_with_prev> stack;
		stack.push_range(
			append_predecessor(sm::node_ref(start), start.adjacent_bones())
		);
		while (!stack.empty()) {
			auto item = stack.top();
			stack.pop();
			if (!visitor(item)) {
				return;
			}
			stack.push_range(
				append_predecessor(item.current, neighbor_bones(item))
			);
		}
	}

    class node_or_bone_visitor {
        sm::node_visitor node_visit_;
        sm::bone_visitor bone_visit_;

    public:
        node_or_bone_visitor(const sm::node_visitor& j, sm::bone_visitor& b) :
                node_visit_(j),
                bone_visit_(b) {
        }

        bool operator()(sm::node_ref j_ref) {
            return (node_visit_) ? node_visit_(j_ref.get()) : true;
        }

        bool operator()(sm::bone_ref b_ref) {
            return (bone_visit_) ? bone_visit_(b_ref.get()) : true;
        }
    };

    class node_or_bone_neighbors_visitor {
        bool downstream_;
    public:
        node_or_bone_neighbors_visitor(bool downstream) : 
            downstream_(downstream)
        {}

        std::vector<node_or_bone> operator()(sm::node_ref j_ref) {
            auto& node = j_ref.get();
            auto neighbors = node.child_bones() |
                rv::transform(
                    [](sm::bone_ref child)->node_or_bone {
                        return child;
                    }
                ) | r::to<std::vector<node_or_bone>>();
            if (!downstream_) {
                if (node.parent_bone().has_value()) {
                    neighbors.push_back(node.parent_bone().value());
                }
            }
            return neighbors;
        }

        std::vector<node_or_bone> operator()(sm::const_bone_ref b_ref) {
            auto& bone = b_ref.get();
            std::vector<node_or_bone> neighbors;
            if (!downstream_) {
                neighbors.push_back(std::ref(bone.parent_node()));
            }
            neighbors.push_back(std::ref(bone.child_node()));
            return neighbors;
        }
    };

    uint64_t get_id(const node_or_bone& var) {
        auto ptr = std::visit(
            [](auto val_ref)->void* {
                auto& val = val_ref.get();
                return reinterpret_cast<void*>(&val);
            },
            var
        );
        return reinterpret_cast<uint64_t>(ptr);
    }

    uint64_t get_id(const sm::bone& var) {
        return reinterpret_cast<uint64_t>(&var);
    }

    uint64_t get_id(const sm::node& var) {
        return reinterpret_cast<uint64_t>(&var);
    }

    std::unordered_map<sm::bone*, double> build_bone_length_table(sm::node& j) {
        std::unordered_map<sm::bone*, double> length_tbl;
        auto visit_bone = [&length_tbl](sm::bone& b)->bool {
            length_tbl[&b] = b.scaled_length();
            return true;
        };
        sm::dfs(j, {}, visit_bone);
        return length_tbl;
    }

    struct targeted_node {
        sm::node& node;
        sm::point target_pos;
        std::optional<sm::point> prev_pos;

        targeted_node(sm::node& j, const sm::point& pt) :
            node(j), target_pos(pt), prev_pos()
        {}
    };

    std::vector<targeted_node> find_pinned_nodes(sm::node& node) {
        std::vector<targeted_node> pinned_nodes;
        auto visit_node = [&pinned_nodes](sm::node& j)->bool {
            if (j.is_pinned()) {
                pinned_nodes.emplace_back(j, j.world_pos());
            }
            return true;
        };
        sm::dfs(node, visit_node, {}, false);
        return pinned_nodes;
    }

    double max_pinned_node_dist(const std::vector<targeted_node>& pinned_nodes) {
        if (pinned_nodes.empty()) {
            return 0;
        }
        return r::max(
            pinned_nodes | rv::transform(
                [](const auto& pj)->double {
                    return sm::distance(pj.node.world_pos(), pj.target_pos);
                }
            )
        );
    }

    sm::matrix rotate_about_point(const sm::point& pt, double theta) {
        return sm::translation_matrix(pt) *
            sm::rotation_matrix(theta) *
            sm::translation_matrix(-pt);
    }

    double angle_from_u_to_v(const sm::point& u, const sm::point& v) {
        auto diff = v - u;
        return std::atan2(diff.y, diff.x);
    }

    sm::point point_on_line_at_distance(const sm::point& u, const sm::point& v, double d) {
        auto pt = sm::point{ u.x + d, u.y };
        return sm::transform(pt, rotate_about_point(u, angle_from_u_to_v(u, v)));
    }

	double apply_angle_constraint(double fixed_rotation, double free_rotation,
		double min_angle, double max_angle, bool fixed_bone_is_anchor) {

		if (!fixed_bone_is_anchor) {
			double old_min = min_angle;
			double old_max = max_angle;
			min_angle = -old_max;
			max_angle = -old_min;
		}

		auto theta = sm::normalize_angle(free_rotation - fixed_rotation);
		if (theta < min_angle || theta > max_angle) {
			auto dist_to_min = std::abs(sm::angular_distance(theta, min_angle));
			auto dist_to_max = std::abs(sm::angular_distance(theta, max_angle));
			theta = (dist_to_min < dist_to_max) ? min_angle : max_angle;
		}

		return sm::normalize_angle(theta + fixed_rotation);
	}

	sm::point apply_angle_constraint(const sm::point& fixed_pt1, const sm::point& fixed_pt2,
		const sm::point& free_pt, double min_angle, double max_angle, bool fixed_bone_is_anchor) {

		auto fixed_rotation = angle_from_u_to_v(fixed_pt1, fixed_pt2);
		auto free_rotation = angle_from_u_to_v(fixed_pt2, free_pt);
		auto new_free_rotation = apply_angle_constraint(
			fixed_rotation, free_rotation,
			min_angle,
			max_angle,
			fixed_bone_is_anchor
		);
		return sm::transform(
			sm::point{ sm::distance(fixed_pt2, free_pt), 0.0 },
			translation_matrix(fixed_pt2)* sm::rotation_matrix(new_free_rotation)
		);
	}

	double apply_parent_child_constraint(const sm::bone& parent, double rotation,
		double min_angle, double max_angle) {
		return apply_angle_constraint(
			parent.world_rotation(), rotation, min_angle, max_angle, true
		);
	}

	struct rot_constraint_info {
		bool is_parent_child;
		bool forward;
		double min;
		double max;
	};

	std::optional<rot_constraint_info> get_forward_rot_constraint(
			const sm::bone& pred, const sm::bone& curr) {
		auto curr_constraint = curr.rotation_constraint();
		if (curr_constraint) {
			if (&curr.parent_bone()->get() != &pred) {
				return {};
			}
			// there is a forward parent child constraint...
			return rot_constraint_info{
				true,
				true,
				curr_constraint->min_angle,
				curr_constraint->max_angle
			};
		}
		return {};
	}

	std::optional<rot_constraint_info> get_backward_rot_constraint(
			const sm::bone& pred, const sm::bone& curr) {
		// a backward parent-child constraint occurs when the predecessor bone
		// has a relative-to-parent rotation constraint and the current bone
		// is the predecessor's parent.

		auto pred_constraint = pred.rotation_constraint();
		if (!pred_constraint || pred_constraint->relative_to_parent == false) {
			return {};
		}
		if (&pred.parent_bone()->get() != &curr) {
			return {};
		}
		return rot_constraint_info{
			true,
			false,
			pred_constraint->min_angle,
			pred_constraint->max_angle
		};
	}

	std::optional<rot_constraint_info>  get_rot_constraint(const sm::bone& pred, const sm::bone& curr) {
		//TODO: test for absolute constraint
		
		auto forward = get_forward_rot_constraint(pred, curr);
		if (forward) {
			return forward;
		}
		return get_backward_rot_constraint(pred, curr);
		
	}

	void perform_one_fabrik_pass(sm::node& start_node, const sm::point& target_pt,
		const std::unordered_map<sm::bone*, double>& bone_lengths, bool ignore_constraints) {

		auto perform_fabrik_on_bone = [&](bone_with_prev& bwp) {

			auto& current_bone = bwp.current.get();
			auto pred_bone = predecessor_bone(bwp);
			auto& leader_node = current_node(bwp);
			auto& follower_node = current_bone.opposite_node(leader_node);

			auto new_follower_pos = point_on_line_at_distance(
				leader_node.world_pos(), 
				follower_node.world_pos(), 
				bone_lengths.at(&current_bone)
			);

			if (pred_bone && !ignore_constraints) {
				auto& pred = pred_bone->get();
				auto rci = get_rot_constraint(pred, current_bone);
				if (rci) {
					auto& fixed_bone = pred_bone->get();
					auto& pred_node = fixed_bone.opposite_node(leader_node);
					new_follower_pos = apply_angle_constraint(
						pred_node.world_pos(), leader_node.world_pos(),
						new_follower_pos, rci->min, rci->max,
						rci->forward
					);
				}
			}
			follower_node.set_world_pos(new_follower_pos);
			return true;
		};

		start_node.set_world_pos(target_pt);
		dfs_bones_with_prev(start_node, perform_fabrik_on_bone);
	}

    sm::fabrik_result target_satisfaction_state(const targeted_node& tj, double tolerance) {
        // has it reached the target?
        if (sm::distance(tj.node.world_pos(), tj.target_pos) < tolerance) {
            return sm::fabrik_result::target_reached;
        }

        // has it moved since the last iteration?
        if (sm::distance(tj.node.world_pos(), tj.prev_pos.value()) < tolerance) {
            return sm::fabrik_result::converged;
        }

        return sm::fabrik_result::no_solution_found;
    }

	bool is_satisfied(const targeted_node& tj, double tolerance) {
		auto result = target_satisfaction_state(tj, tolerance);
		return result == sm::fabrik_result::target_reached || 
			result == sm::fabrik_result::converged;
	}

    bool found_ik_solution( std::span<targeted_node> targeted_nodes, double tolerance) {
        auto unsatisfied = r::find_if(targeted_nodes,
            [tolerance](const auto& tj)->bool {
                return !is_satisfied(tj, tolerance);
            }
        );
        return (unsatisfied == targeted_nodes.end());
    }

    void  update_prev_positions(std::vector<targeted_node>& targeted_nodes) {
        for (auto& tj : targeted_nodes) {
            tj.prev_pos = tj.node.world_pos();
        }
    }

    void solve_for_multiple_targets(std::span<targeted_node> targeted_nodes, 
            double tolerance, const std::unordered_map<sm::bone*, double>& bone_lengths,
            int max_iter, bool ignore_constraints) {
        int j = 0;
        do {
            if (++j > max_iter) {
                return;
            }
            if (j % 2 == 0) {
                for (auto& pinned_node : targeted_nodes) {
                    perform_one_fabrik_pass(
						pinned_node.node, pinned_node.target_pos, bone_lengths, ignore_constraints
					);
                }
            } else {
                for (auto& pinned_node : targeted_nodes | rv::reverse) {
                    perform_one_fabrik_pass(
						pinned_node.node, pinned_node.target_pos, bone_lengths, ignore_constraints
					);
                }
            }
		} while (!found_ik_solution(targeted_nodes, tolerance));
    }
}

/*------------------------------------------------------------------------------------------------*/

sm::node::node(const std::string& name, double x, double y) :
    name_(name),
    x_(x),
    y_(y),
    is_pinned_(false)
{}

void sm::node::set_parent(bone& b) {
    parent_ = std::ref(b);
}

void sm::node::add_child(bone& b) {
    children_.push_back(std::ref(b));
}

std::string sm::node::name() const {
    return name_;
}

sm::maybe_bone_ref sm::node::parent_bone() const {
    return parent_;
}

std::span<const sm::bone_ref> sm::node::child_bones() const {
    return children_;
}

std::vector<sm::bone_ref> sm::node::adjacent_bones() const {
	std::vector<sm::bone_ref> bones;
	bones.reserve(children_.size() + 1);
	if (!is_root()) {
		bones.push_back(*parent_);
	}
	r::copy(children_, std::back_inserter(bones));
	bones.shrink_to_fit();
	return bones;
}

double sm::node::world_x() const {
    return x_;
}

double sm::node::world_y() const {
    return y_;
}

void sm::node::set_world_pos(const point& pt) {
    x_ = pt.x;
    y_ = pt.y;
}

sm::point sm::node::world_pos() const {
    return { x_, y_ };
}

std::any sm::node::get_user_data() const {
    return user_data_;
}

void sm::node::set_user_data(std::any data) {
    user_data_ = data;
}

bool sm::node::is_root() const {
    return !parent_.has_value();
}

bool sm::node::is_pinned() const {
    return  is_pinned_;
}

void sm::node::set_pinned(bool pinned) {
    is_pinned_ = pinned;
}

/*------------------------------------------------------------------------------------------------*/

sm::bone::bone(std::string name, sm::node& u, sm::node& v) :
       name_(name), u_(u), v_(v) {
    v.set_parent(*this);
    u.add_child(*this);
    length_ = scaled_length();
}

sm::result sm::bone::set_rotation_constraint(double min, double max, bool relative_to_parent) { 
	if (relative_to_parent && !parent_bone()) {
		return result::no_parent_bone;
	}
	rot_constraint_ = rot_constraint{ relative_to_parent, min, max };
	return result::no_error;
}

std::optional<sm::rot_constraint> sm::bone::rotation_constraint() const {
	return rot_constraint_;
}

void sm::bone::remove_rotation_constraint() { 
	rot_constraint_ = {};
}

std::string sm::bone::name() const {
    return name_;
}

sm::node& sm::bone::parent_node() const {
    return u_;
}

sm::node& sm::bone::child_node() const {
    return v_;
}

sm::node& sm::bone::opposite_node(const node& j) const {
    if (&j == &u_) {
        return v_;
    } else {
        return u_;
    }
}

std::optional<sm::node_ref> sm::bone::shared_node(const bone& b) const {
	auto* b_u = &b.parent_node();
	auto* b_v = &b.child_node();
	if (&u_ == b_u) {
		return sm::node_ref(*b_u);
	} else if (&u_ == b_v) {
		return sm::node_ref(*b_v);
	} else if (&v_ == b_u) {
		return sm::node_ref(*b_u);
	} else if (&v_ == b_v) {
		return sm::node_ref(*b_v);
	}
	return {};
}

std::tuple<sm::point, sm::point> sm::bone::line_segment() const {
    return { u_.world_pos(), v_.world_pos() };
}

double sm::bone::length() const {
    return length_;
}

double sm::bone::scaled_length() const {
    return std::apply(distance, line_segment());
}

double sm::bone::world_rotation() const {
    auto [u, v] = line_segment();
    return std::atan2(
        (v.y - u.y),
        (v.x - u.x)
    );
}

double sm::bone::rotation() const {
    auto parent = parent_bone();
    if (!parent) {
        return world_rotation();
    }
    return world_rotation() - parent->get().world_rotation();
}

double sm::bone::scale() const {
    auto parent = parent_bone();
    if (!parent) {
        return absolute_scale();
    }
    return absolute_scale() / parent->get().absolute_scale();
}

double sm::bone::absolute_scale() const {
    return scaled_length() / length();
}

sm::maybe_bone_ref  sm::bone::parent_bone() const {
    return u_.parent_bone();
}

std::any sm::bone::get_user_data() const {
    return user_data_;
}

void sm::bone::set_user_data(std::any data) {
    user_data_ = data;
}

void sm::bone::rotate(double theta) {
	//TODO
	/*
	auto maybe_constraint = parent_constraint_on_bone(*this);
	if (maybe_constraint) {
		theta = apply_parent_child_constraint(
			parent_bone()->get(),
			theta + world_rotation(),
			maybe_constraint->min,
			maybe_constraint->max
		) - world_rotation();
	}
	*/
    auto rotate_mat = rotate_about_point(u_.world_pos(), theta);

    auto rotate_about_u = [&rotate_mat](node& j)->bool {
        j.set_world_pos(transform(j.world_pos(), rotate_mat));
        return true;
    };
	
    visit_nodes(v_, rotate_about_u);
}

/*------------------------------------------------------------------------------------------------*/

sm::ik_sandbox::ik_sandbox() : node_id_(0), bone_id_(0) {};

std::expected<sm::node_ref, sm::result> sm::ik_sandbox::create_node(
        const std::string& node_name, double x, double y) {
    auto name = (node_name.empty()) ? "node-" + std::to_string(++node_id_) : node_name;
    if (nodes_.contains(name)) {
        return std::unexpected{ sm::result::non_unique_name };
    }
    nodes_[name] = node::make_unique(name, x, y);
    return std::ref( *nodes_[name] );
}

bool sm::ik_sandbox::set_node_name(node& j, const std::string& name) {
    if (nodes_.contains(name)) {
        return false;
    }
    nodes_[name] = std::move(nodes_[j.name()]);
    nodes_.erase(j.name());
    return true;
}

std::expected<sm::bone_ref, sm::result> sm::ik_sandbox::create_bone(
        const std::string& bone_name, node& u, node& v) {

    if (!v.is_root()) {
        return std::unexpected(sm::result::multi_parent_node);
    }

    if (is_reachable(u, v)) {
        return std::unexpected(sm::result::cyclic_bones);
    }

    auto name = (bone_name.empty()) ? "bone-" + std::to_string(++bone_id_) : bone_name;
    if (bones_.contains(name)) {
        return std::unexpected{ sm::result::non_unique_name };
    }

    bones_[name] = bone::make_unique(name, u, v);
    return std::ref( *bones_[name] );
}

bool sm::ik_sandbox::set_bone_name(sm::bone& b, const std::string& name) {
    if (bones_.contains(name)) {
        return false;
    }
    bones_[name] = std::move(bones_[b.name()]);
    bones_.erase(b.name());
    return false;
}

bool sm::ik_sandbox::is_reachable(node& j1, node& j2) {
    auto found = false;
    auto visit_node = [&found, &j2](node& j) {
        if (&j == &j2) {
            found = true;
            return false;
        }
        return true;
    };
    dfs(j1, visit_node);
    return found;
}

void sm::dfs(node& j1, node_visitor visit_node, bone_visitor visit_bone,
        bool just_downstream) {
    std::stack<node_or_bone> stack;
    std::unordered_set<uint64_t> visited;
    node_or_bone_visitor visitor(visit_node, visit_bone);
    node_or_bone_neighbors_visitor neighbors_visitor(just_downstream);
    stack.push(std::ref(j1));

    while (!stack.empty()) {
        auto item = stack.top();
        stack.pop();
        auto id = get_id(item);
        if (visited.contains(id)) {
            continue;
        }
        auto result = std::visit(visitor, item);
        visited.insert(id);
        if (!result) {
            return;
        }
        auto neighbors = std::visit(neighbors_visitor, item);
        for (const auto& neighbor : neighbors) {
            stack.push(neighbor);
        }
    }
}

void sm::visit_nodes(node& j, node_visitor visit_node) {
    dfs(j, visit_node, {}, true);
}

sm::fabrik_result sm::perform_fabrik(sm::node& effector, const sm::point& target_pt,
        double tolerance, int max_iter) {

    auto bone_lengths = build_bone_length_table(effector);
    auto targeted_nodes = find_pinned_nodes(effector);
    targeted_nodes.emplace_back(effector, target_pt);
    auto& target = targeted_nodes.back();
    auto pinned_nodes = std::span{ targeted_nodes.begin(), std::prev(targeted_nodes.end())};
	auto has_pinned_nodes = !pinned_nodes.empty();
    int iter = 0;

    do {
        if (++iter >= max_iter) {
			return fabrik_result::no_solution_found;
        }
        update_prev_positions(targeted_nodes);

        // reach for target from effector...
        perform_one_fabrik_pass(target.node, target.target_pos, bone_lengths, has_pinned_nodes); 

        // reach for pinned locations from pinned nodes
		if (has_pinned_nodes) {
			solve_for_multiple_targets(pinned_nodes, tolerance, bone_lengths, max_iter, false);
		}
	} while (!found_ik_solution(targeted_nodes, tolerance));

	return (target_satisfaction_state(target, tolerance) == fabrik_result::target_reached) ?
		fabrik_result::target_reached :
		fabrik_result::converged;
}



