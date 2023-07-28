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

#include <sstream> // TODO: remove debug code

namespace r = std::ranges;
namespace rv = std::ranges::views;

/*------------------------------------------------------------------------------------------------*/

namespace {

    using node_or_bone = std::variant<sm::bone_ref, sm::node_ref>;

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

    std::unordered_map<uint64_t, double> build_bone_length_table(sm::node& j) {
        std::unordered_map<uint64_t, double> length_tbl;
        auto visit_bone = [&length_tbl](sm::bone& b)->bool {
            length_tbl[get_id(b)] = b.scaled_length();
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

    void reach(sm::bone& bone, double length, sm::node& leader, sm::point target) {
        leader.set_world_pos(target);
        sm::node& follower = bone.opposite_node(leader);
        auto new_follower_pos = point_on_line_at_distance(
            leader.world_pos(), follower.world_pos(), length
        );
        follower.set_world_pos(new_follower_pos);
    }

    void perform_one_fabrik_pass(sm::node& j, const sm::point& pt, 
            const std::unordered_map<uint64_t, double>& bone_lengths) {
        std::unordered_set<uint64_t> moved_nodes;

        auto perform_fabrik_on_bone = [&](sm::bone& b) {
            sm::node& moved_node = moved_nodes.contains(get_id(b.parent_node())) ?
                b.parent_node() :
                b.child_node();
            if (moved_nodes.contains(get_id(b.opposite_node(moved_node)))) {
                return true;
            }
            reach(b, bone_lengths.at(get_id(b)), moved_node, moved_node.world_pos());
            moved_nodes.insert(get_id(b.opposite_node(moved_node)));
            return true;
        };

        j.set_world_pos(pt);
        moved_nodes.insert(get_id(j));
        dfs(j, {}, perform_fabrik_on_bone, false);
    }

    bool is_satisfied(const targeted_node& tj, double tolerance) {
        if (!tj.prev_pos) {
            // ensure that at least one pass of FABRIK happens...
            return false;
        }

        // has it reached the target?
        if (sm::distance(tj.node.world_pos(), tj.target_pos) < tolerance) {
            return true;
        }

        // has it moved since the last iteration?
        if (sm::distance(tj.node.world_pos(), tj.prev_pos.value()) < tolerance) {
            return true;
        }

        return false;
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
            double tolerance, const std::unordered_map<uint64_t, double>& bone_lengths,
            int max_iter) {
        int j = 0;
        while (!found_ik_solution(targeted_nodes, tolerance)) {
            if (++j > max_iter) {
                return;
            }
            if (j % 2 == 0) {
                for (auto& pinned_node : targeted_nodes) {
                    perform_one_fabrik_pass(pinned_node.node, pinned_node.target_pos, bone_lengths);
                }
            } else {
                for (auto& pinned_node : targeted_nodes | rv::reverse) {
                    perform_one_fabrik_pass(pinned_node.node, pinned_node.target_pos, bone_lengths);
                }
            }
        }
    }

	std::optional<sm::angle_range> parent_constraint_on_bone(const sm::bone& bone) {
		if (!bone.parent_bone()) {
			return {};
		}
		const auto& parent_node = bone.parent_node();
		const auto& parent_bone = bone.parent_bone()->get();
		return parent_node.constraint_angles(parent_bone, bone);
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

sm::result sm::node::set_constraint(const bone& parent, const bone& dependent, double min, double max) {
	auto constraint = r::find_if(constraints_,
		[&parent, &dependent](const auto& jc) {
			return &jc.anchor_bone.get() == &parent && &jc.dependent_bone.get() == &dependent;
		}
	);
	if (constraint == constraints_.end()) {
		joint_constraint c{ std::cref(parent), std::cref(dependent), 0.0, 0.0 };
		constraints_.emplace_back(c);
		constraint = std::prev(constraints_.end());
	} 
	constraint->angles.min = min;
	constraint->angles.max = max;

	return result::no_error;
}

std::optional<sm::angle_range> sm::node::constraint_angles(
		const bone& parent, const bone& dependent) const {
	auto constraint = r::find_if(constraints_,
		[&parent, &dependent](const auto& jc) {
			return &jc.anchor_bone.get() == &parent && &jc.dependent_bone.get() == &dependent;
		}
	);
	if (constraint == constraints_.end()) {
		return {};
	}
	return constraint->angles;
}

sm::result sm::node::remove_constraint(const bone& parent, const bone& dependent) {
	auto constraint = r::find_if(constraints_,
		[&parent, &dependent](const auto& jc) {
			return &jc.anchor_bone.get() == &parent && &jc.dependent_bone.get() == &dependent;
		}
	);
	if (constraint == constraints_.end()) {
		return result::constraint_not_found;
	}
	constraints_.erase(constraint);
	return result::no_error;
}

/*------------------------------------------------------------------------------------------------*/

sm::bone::bone(std::string name, sm::node& u, sm::node& v) :
       name_(name), u_(u), v_(v) {
    v.set_parent(*this);
    u.add_child(*this);
    length_ = scaled_length();
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

	auto maybe_constraint = parent_constraint_on_bone(*this);
	if (maybe_constraint) {
		theta = apply_parent_child_constraint(
			parent_bone()->get(),
			theta + world_rotation(),
			maybe_constraint->min,
			maybe_constraint->max
		) - world_rotation();
	}
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

struct bone_pair {
	node_or_bone prev;
	sm::bone_ref current;

	bone_pair(sm::node& n, sm::bone& b) : prev{n}, current{ b } {
	}

	bone_pair(sm::bone_ref p, sm::bone_ref b) : prev{p}, current{ b } {
	}


};

using bone_pair_visitor = std::function<bool(bone_pair&)>;

auto to_bone_pairs(auto prev, const std::vector<sm::bone_ref>& bones) {
	return bones | rv::transform([prev](auto&& b) {return bone_pair{ prev, b }; });
}

sm::node& current_node(const bone_pair& bones) {
	if (std::holds_alternative<sm::node_ref>(bones.prev)) {
		return std::get<sm::node_ref>(bones.prev).get();
	} else {
		sm::bone& prev_bone = std::get<sm::bone_ref>(bones.prev).get();
		auto shared = bones.current.get().shared_node(prev_bone);
		if (!shared) {
			throw std::runtime_error("bad call to dfs_bones_with_prev");
		}
		return shared->get();
	}
}

std::vector<sm::bone_ref> neighbor_bones(const bone_pair& pair) {
	sm::node& curr_node = current_node(pair);
	sm::node& next_node = pair.current.get().opposite_node(curr_node);
	return next_node.adjacent_bones() | 
		rv::filter(
			[&pair](sm::bone_ref b) { return &(b.get()) != &pair.current.get(); }
	) | r::to<std::vector<sm::bone_ref>>();
}

void dfs_bones_with_prev(sm::node& start, bone_pair_visitor visitor) {
	std::stack<bone_pair> stack;
	stack.push_range(to_bone_pairs(sm::node_ref(start), start.adjacent_bones()));
	while (!stack.empty()) {
		auto item = stack.top();
		stack.pop();

		auto result = visitor(item);
		if (!result) {
			return;
		}
		stack.push_range(
			to_bone_pairs(item.current, neighbor_bones(item))
		);
	}
}

std::string sm::debug(sm::node& node) {
	std::stringstream ss;

	dfs_bones_with_prev(
		node,
		[&ss](bone_pair& bones)->bool {
			std::string prev_name = std::visit(
				[](auto n_or_b)->std::string { return n_or_b.get().name(); },
				bones.prev
			);
			ss << "[ "  << prev_name << " , " << bones.current.get().name() << " ]\n";
			return true;
		}
	);
	return ss.str();
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

void sm::debug_reach(node& j, sm::point pt) {
    auto maybe_bone_ref = j.parent_bone();
    auto& bone = maybe_bone_ref->get();
    reach(bone, bone.scaled_length(), j, pt);
}

void sm::perform_fabrik(sm::node& effector, const sm::point& target_pt, 
        double tolerance, int max_iter) {

    auto bone_lengths = build_bone_length_table(effector);
    auto targeted_nodes = find_pinned_nodes(effector);
    targeted_nodes.emplace_back(effector, target_pt);
    auto& target = targeted_nodes.back();
    auto pinned_nodes = std::span{ targeted_nodes.begin(), std::prev(targeted_nodes.end())};
    
    int iter = 0;
    while (! found_ik_solution(targeted_nodes, tolerance)) {
        if (++iter >= max_iter) {
            break;
        }
        update_prev_positions(targeted_nodes);

        // reach for target from effector...
        perform_one_fabrik_pass(target.node, target.target_pos, bone_lengths);

        // reach for pinned locations from pinned nodes
        solve_for_multiple_targets(pinned_nodes, tolerance, bone_lengths, max_iter);
    }
}

double sm::apply_angle_constraint(double fixed_rotation, double free_rotation,
		double min_angle, double max_angle, bool fixed_bone_is_anchor) {

	max_angle = fixed_bone_is_anchor ? max_angle : -min_angle;
	min_angle = fixed_bone_is_anchor ? min_angle : -max_angle;

	auto theta = normalize_angle(free_rotation - fixed_rotation);
	theta = (theta < min_angle) ? min_angle : theta;
	theta = (theta > max_angle) ? max_angle : theta;

	return normalize_angle(theta + fixed_rotation);
}

sm::point sm::apply_angle_constraint(const point& fixed_pt1, const point& fixed_pt2, 
		const point& free_pt, double min_angle, double max_angle, bool fixed_bone_is_anchor) {

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
		translation_matrix(fixed_pt2) * rotation_matrix(new_free_rotation)
	);
}

double sm::apply_parent_child_constraint(const sm::bone& parent, double rotation,
		double min_angle, double max_angle) {
	return apply_angle_constraint(parent.world_rotation(), rotation, min_angle, max_angle, true);
}

