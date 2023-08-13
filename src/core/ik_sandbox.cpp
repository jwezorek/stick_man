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

    using node_or_bone = std::variant<sm::bone_ref, sm::node_ref>;

	// The core of the implementation of the FABRIK ik algorithm is a DFS over the bones in the
	// skeleton. At any step in the algorithm the current bone along with its predecessor in the
	// dfs traversal may be the neighborhood of one or more rotational constraints. This
	// fabrik item class models such a neighborhood.

	class fabrik_item {
		node_or_bone pred_;
		sm::bone_ref current_;
		sm::node* current_node_;
		mutable sm::node* pred_node_;

	public:

		fabrik_item(node_or_bone p, sm::bone_ref b) :
			pred_{ p }, 
			current_{ b },
			current_node_(nullptr),
			pred_node_(nullptr) {
		}

		fabrik_item(sm::bone& b) :
			pred_{ 
				(b.parent_bone()) ? 
					node_or_bone{*b.parent_bone()} :
					node_or_bone{b.parent_node()}
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
				} else {
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
		// not sibling bones unless this is the root. this forces a traversal in which it is impossible
		// for an applicable rotation constraint to be relative to a bone that is not
		// the predecessor bone.(because we do not currently support sibling constraints)

		std::vector<sm::bone_ref> neighbor_bones(const std::unordered_set<sm::bone*>& visited) {
			auto neighbors = current_bone().child_bones();
			auto parent = current_bone().parent_bone();
			if (parent) {
				neighbors.push_back(*parent);
			} else {
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
		return bones | rv::transform([prev](auto b) {return fabrik_item( prev, b ); });
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

    class node_or_bone_visitor {
        sm::node_visitor node_visit_;
        sm::bone_visitor bone_visit_;

    public:
        node_or_bone_visitor(const sm::node_visitor& j, sm::bone_visitor& b) :
                node_visit_(j),
                bone_visit_(b) {
        }

        bool operator()(sm::node_ref j_ref) {
            return (node_visit_) ? node_visit_(j_ref) : true;
        }

        bool operator()(sm::bone_ref b_ref) {
            return (bone_visit_) ? bone_visit_(b_ref) : true;
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

        std::vector<node_or_bone> operator()(sm::bone_ref b_ref) {
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

	struct bone_info {
		double length;
		double rotation;
	};

    std::unordered_map<sm::bone*, bone_info> build_bone_table(sm::node& j) {
        std::unordered_map<sm::bone*, bone_info> length_tbl;
        auto visit_bone = [&length_tbl](sm::bone& b)->bool {
			length_tbl[&b] = { b.scaled_length(), b.world_rotation() };
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

	sm::point apply_rotation_constraints( const fabrik_item& fi, const sm::point& free_pt ) {

		auto pivot_pt = fi.current_node().world_pos();
		auto old_theta = angle_from_u_to_v(pivot_pt, free_pt);
		auto new_theta = apply_rotation_constraints(fi, old_theta);

		if (!new_theta) {
			return free_pt;
		}

		return sm::transform(
			sm::point{ sm::distance(pivot_pt, free_pt), 0.0 },
			translation_matrix(pivot_pt)* sm::rotation_matrix(*new_theta)
		);

	}

	sm::point constraint_angular_velocity(
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
			translation_matrix(pivot_node.world_pos())* sm::rotation_matrix(new_theta)
		);
	}

	void perform_one_fabrik_pass(sm::node& start_node, const sm::point& target_pt,
		const std::unordered_map<sm::bone*, bone_info>& bone_tbl, bool use_constraints) {

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
				new_follower_pos = apply_rotation_constraints( fi, new_follower_pos );
			}
			
			new_follower_pos = constraint_angular_velocity(
				fi, bone_tbl.at(&current_bone).rotation, 
				1.0 * std::numbers::pi / 180.0,
				new_follower_pos
			);

			follower_node.set_world_pos(new_follower_pos);
			return true;
		};

		start_node.set_world_pos(target_pt);
		fabrik_traversal(start_node, perform_fabrik_on_bone);
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
            double tolerance, const std::unordered_map<sm::bone*, bone_info>& bone_tbl,
            int max_iter, bool use_constraints) {
        int j = 0;
        do {
            if (++j > max_iter) {
                return;
            }
            if (j % 2 == 0) {
                for (auto& pinned_node : targeted_nodes) {
                    perform_one_fabrik_pass(
						pinned_node.node, pinned_node.target_pos, bone_tbl, use_constraints
					);
                }
            } else {
                for (auto& pinned_node : targeted_nodes | rv::reverse) {
                    perform_one_fabrik_pass(
						pinned_node.node, pinned_node.target_pos, bone_tbl, use_constraints
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

sm::maybe_bone_ref sm::node::parent_bone() {
	return parent_;
}

sm::maybe_const_bone_ref sm::node::parent_bone() const {
	return parent_;
}

std::vector<sm::bone_ref> sm::node::child_bones() {
	return children_;
}

std::vector<sm::const_bone_ref> sm::node::child_bones() const {
	return children_ |
		rv::transform([](const auto& c) {return sm::const_bone_ref(c); }) |
		r::to< std::vector<sm::const_bone_ref>>();
}

std::vector<sm::bone_ref> sm::node::adjacent_bones() {
	std::vector<sm::bone_ref> bones;
	bones.reserve(children_.size() + 1);
	if (!is_root()) {
		bones.push_back(*parent_);
	}
	r::copy(children_, std::back_inserter(bones));
	bones.shrink_to_fit();
	return bones;
}

std::vector<sm::const_bone_ref> sm::node::adjacent_bones() const {
	auto* non_const_this = const_cast<sm::node*>(this);
	return non_const_this->adjacent_bones() |
		rv::transform([](const auto& c) {return sm::const_bone_ref(c); }) |
		r::to< std::vector<sm::const_bone_ref>>();
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
		return result::no_parent;
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

sm::node& sm::bone::parent_node() {
	return u_;
}

const sm::node& sm::bone::parent_node() const {
	auto* non_const_this = const_cast<sm::bone*>(this);
	return non_const_this->parent_node();
}

sm::node& sm::bone::child_node() {
	return v_;
}

const sm::node& sm::bone::child_node() const {
	auto* non_const_this = const_cast<sm::bone*>(this);
	return non_const_this->child_node();
}

sm::node& sm::bone::opposite_node( const node& j) {
	if (&j == &u_) {
		return v_;
	} else {
		return u_;
	}
}

std::vector<sm::bone_ref> sm::bone::child_bones() {
	return v_.child_bones();
}

std::vector<sm::const_bone_ref> sm::bone::child_bones() const {
	return const_cast<const sm::node&>(v_).child_bones();
}

std::vector<sm::bone_ref> sm::bone::sibling_bones() {
	return u_.child_bones() |
		rv::filter(
			[this](bone_ref sib) {
				return &sib.get() != this;
			}
	) | r::to< std::vector<sm::bone_ref>>();
}

const sm::node& sm::bone::opposite_node(const node& j) const {
	auto* non_const_this = const_cast<sm::bone*>(this);
	return non_const_this->opposite_node(j);
}

std::optional<sm::node_ref> sm::bone::shared_node(const bone& b) {
	auto* b_u = &b.parent_node();
	auto* b_v = &b.child_node();
	if (&u_ == b_u || (&u_ == b_v)) {
		return u_;
	} else if (&v_ == b_u || &v_ == b_v) {
		return v_;
	}
	return {};
}

std::optional<sm::const_node_ref> sm::bone::shared_node(const bone& b) const {
	auto* non_const_this = const_cast<sm::bone*>(this);
	return non_const_this->shared_node(b);
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

sm::maybe_const_bone_ref  sm::bone::parent_bone() const {
    return u_.parent_bone();
}

sm::maybe_bone_ref  sm::bone::parent_bone() {
	return u_.parent_bone();
}

std::any sm::bone::get_user_data() const {
    return user_data_;
}

void sm::bone::set_user_data(std::any data) {
    user_data_ = data;
}

void sm::bone::rotate(double theta) {

	auto new_theta = apply_rotation_constraints(fabrik_item(*this), theta + world_rotation());
	theta = new_theta ? *new_theta - world_rotation() : theta;

    auto rotate_mat = rotate_about_point_matrix(u_.world_pos(), theta);

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

    auto bone_tbl = build_bone_table(effector);
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
        perform_one_fabrik_pass(target.node, target.target_pos, bone_tbl, !has_pinned_nodes); 

        // reach for pinned locations from pinned nodes
		if (has_pinned_nodes) {
			solve_for_multiple_targets(pinned_nodes, tolerance, bone_tbl, max_iter, true);
		}
	} while (!found_ik_solution(targeted_nodes, tolerance));

	return (target_satisfaction_state(target, tolerance) == fabrik_result::target_reached) ?
		fabrik_result::target_reached :
		fabrik_result::converged;
}



