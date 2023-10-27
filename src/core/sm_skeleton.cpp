#include "sm_skeleton.h"
#include "sm_types.h"
#include "json.hpp"
#include <cmath>
#include <variant>
#include <stack>
#include <unordered_set>
#include <unordered_map>
#include <limits>
#include <numbers>
#include <functional>
#include <tuple>
#include <span>
#include <map>

using namespace std::placeholders;

namespace r = std::ranges;
namespace rv = std::ranges::views;
using json = nlohmann::json;

/*------------------------------------------------------------------------------------------------*/

namespace {

	template<class... Ts> struct overloaded : Ts... { using Ts::operator()...; }; 
	template<class... Ts> overloaded(Ts...) -> overloaded<Ts...>;  

	constexpr double k_tolerance = 0.005;
	constexpr int k_max_iter = 100;

    template<typename V, typename E>
    using node_or_bone = std::variant<std::reference_wrapper<V>, std::reference_wrapper<E>>;

	// The core of the implementation of the FABRIK ik algorithm is a DFS over the bones in the
	// skeleton. At any step in the algorithm the current bone along with its predecessor in the
	// dfs traversal may be the neighborhood of one or more rotational constraints. This
	// fabrik item class models such a neighborhood.

	class fabrik_item {
		node_or_bone<sm::node, sm::bone> pred_;
		sm::bone_ref current_;
		sm::node* current_node_;
		mutable sm::node* pred_node_;

	public:

		fabrik_item(node_or_bone<sm::node, sm::bone> p, sm::bone_ref b) :
			pred_{ p }, 
			current_{ b },
			current_node_(nullptr),
			pred_node_(nullptr) {
		}

		fabrik_item(sm::bone& b) :
			pred_{ 
				(b.parent_bone()) ? 
                    node_or_bone<sm::node, sm::bone>{*b.parent_bone()} :
                    node_or_bone<sm::node, sm::bone>{b.parent_node()}
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
		// not sibling bones unless this is the root. this forces a traversal in which it is 
        // impossible for an applicable rotation constraint to be relative to a bone that is 
        // not the predecessor bone.(because we do not currently support sibling constraints)

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

    template <typename T>
    using visitor_fn = std::function<sm::visit_result(T&)>;

    template <typename V, typename E>
    class node_or_bone_visitor {
        visitor_fn<V> node_visit_;
        visitor_fn<E> bone_visit_;

    public:
        node_or_bone_visitor(const visitor_fn<V>& j, visitor_fn<E>& b) :
                node_visit_(j),
                bone_visit_(b) {
        }

        sm::visit_result operator()(std::reference_wrapper<V> j_ref) {
            return (node_visit_) ? node_visit_(j_ref) : sm::visit_result::continue_traversal;
        }

        sm::visit_result operator()(std::reference_wrapper<E> b_ref) {
            return (bone_visit_) ? bone_visit_(b_ref) : sm::visit_result::continue_traversal;
        }
    };

    template<typename V, typename E>
    class node_or_bone_neighbors_visitor {
        bool downstream_;
    public:
        node_or_bone_neighbors_visitor(bool downstream) : 
            downstream_(downstream)
        {}

        std::vector<node_or_bone<V,E>> operator()(std::reference_wrapper<V> j_ref) {
            auto& node = j_ref.get();
            auto neighbors = node.child_bones() |
                rv::transform(
                    [](auto child)->node_or_bone<V, E> {
                        return child;
                    }
                ) | r::to<std::vector<node_or_bone<V,E>>>();
            if (!downstream_) {
                if (node.parent_bone().has_value()) {
                    neighbors.push_back(node.parent_bone().value());
                }
            }
            return neighbors;
        }

        std::vector<node_or_bone<V, E>> operator()(std::reference_wrapper<E> b_ref) {
            auto& bone = b_ref.get();
            std::vector<node_or_bone<V, E>> neighbors;
            if (!downstream_) {
                neighbors.push_back(std::ref(bone.parent_node()));
            }
            neighbors.push_back(std::ref(bone.child_node()));
            return neighbors;
        }
    };

    template<typename V, typename E>
    uint64_t get_id(const node_or_bone<V,E>& var) {
        auto ptr = std::visit(
            []<typename T>(std::reference_wrapper<T> val_ref)->void* {
                auto& val = val_ref.get();
                return reinterpret_cast<void*>(
                    const_cast<std::remove_cv<T>::type*>(&val)
                );
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

	sm::point apply_rotation_constraints( const fabrik_item& fi, const sm::point& free_pt ) {

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
			translation_matrix(pivot_node.world_pos())* sm::rotation_matrix(new_theta)
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
				new_follower_pos = apply_rotation_constraints( fi, new_follower_pos );
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

	json node_to_json(const sm::node& node) {
		return {
			{"name", node.name()},
			{"pos",
				{{"x", node.world_x()}, {"y", node.world_y()} }
			}
		};
	}

	json bone_to_json(const sm::bone& bone) {
		json bone_json = {
			{"name", bone.name()},
			{"u", bone.parent_node().name()},
			{"v", bone.child_node().name()},
		};
		auto constraint = bone.rotation_constraint();
		if (constraint) {
			bone_json["rot_constraint"] = {
				{"relative_to_parent", constraint->relative_to_parent},
				{"start_angle",  constraint->start_angle},
				{"span_angle",  constraint->span_angle}
			};
		}
		return bone_json;
	}

	bool is_prefix(const std::string& prefix, const std::string& str) {
		auto [lhs, rhs] = r::mismatch(prefix, str);
		return lhs == prefix.end();
	}

	// given {foo-3,bar,quux,foo-1,foo-2} return {3, 1, 2}

	std::vector<int> extract_prefixed_integers(const std::string& prefix, const std::vector<std::string>& names) {
		auto n = prefix.size();
		return names | rv::filter(
				std::bind(is_prefix, prefix, _1)
			) | rv::transform(
				[n](const auto& str)->int {
					try {
						auto num_str = str.substr(n, str.size() - n);
						return std::stoi(num_str);
					} catch (...) {
						return 0;
					}
				}
			) | rv::filter(
				[](int v) {return v > 0; }
			) | r::to< std::vector<int>>();
	}

	int smallest_excluded_positive_integer(const std::vector<int>& nums) {
		int n = static_cast<int>(nums.size()) + 1;
		std::vector<bool> appears(n, false);
		appears[0] = true;
		for (auto i : nums) {
			if (i < n) {
				appears[i] = true;
			}
		}
		auto first_false = r::find(appears, false);
		if (first_false == appears.end()) {
			return n;
		}
		return std::distance(appears.begin(), first_false);
	}

	std::string unique_name(const std::string& prefix, const std::vector<std::string>& names) {
		std::string prefix_with_hyphen = prefix + "-";
		std::vector<int> ids_taken = extract_prefixed_integers(prefix_with_hyphen, names);
		auto index = smallest_excluded_positive_integer(ids_taken);
		return prefix + "-" + std::to_string(index);
	}

	template<typename T>
	auto to_name_table(auto itms) {
		using name_table_t = std::multimap<std::string, T*>;
		return itms | rv::transform(
				[](auto r)->name_table_t::value_type {
					return { r.get().name(), &r.get() };
				}
			) | r::to<name_table_t>();
	}

	std::string normalize_name(const std::string& input) {
		if (input.empty()) {
			return "";
		}

		if (std::isdigit(input.back())) {
			size_t hyphenPos = input.find_last_of('-');
			if (hyphenPos != std::string::npos && hyphenPos < input.size() - 1) {
				std::string numPart = input.substr(hyphenPos + 1);
				bool isPositiveInteger = true;
				for (char c : numPart) {
					if (!std::isdigit(c)) {
						isPositiveInteger = false;
						break;
					}
				}
				if (isPositiveInteger) {
					return input.substr(0, hyphenPos);
				}
			}
		}

		return input;
	}

	template<typename T>
	std::vector<std::pair<std::string,T*>> get_unique_names(const std::multimap<std::string, T*>& tbl) {
		std::unordered_map<std::string, int> names;
        std::vector<std::pair<std::string, T*>> output;

		for (const auto& [name, ptr] : tbl) {
			auto new_name_base = normalize_name(name);
			auto index = names[new_name_base]++;
            auto new_name = (index > 0) ?
                new_name_base + "-" + std::to_string(index) :
                new_name_base;
            output.emplace_back(new_name, ptr);
		}
		return output;
	}

	// TODO: remove code duplication via metaprogramming...
	std::vector<sm::node_ref> nodes_from_traversal(sm::node& root) {
		std::vector<sm::node_ref> nodes;
		sm::visit_nodes(
			root,
			[&](sm::node& n)->sm::visit_result {
				nodes.push_back(n);
				return sm::visit_result::continue_traversal;
			}
		);
		return nodes;
	}

	std::vector<sm::bone_ref> bones_from_traversal(sm::node& root) {
		std::vector<sm::bone_ref> bones;
		sm::visit_bones(
			root,
			[&](sm::bone& n)->sm::visit_result {
				bones.push_back(n);
				return sm::visit_result::continue_traversal;
			}
		);
		return bones;
	}

    template<typename V, typename E>
    void dfs_aux(node_or_bone<V,E> root, visitor_fn<V> visit_node, visitor_fn<E> visit_bone,
            bool just_downstream) {
        std::stack<node_or_bone<V, E>> stack;
        std::unordered_set<uint64_t> visited;
        node_or_bone_visitor<V,E> visitor(visit_node, visit_bone);
        node_or_bone_neighbors_visitor<V,E> neighbors_visitor(just_downstream);
        stack.push(root);

        while (!stack.empty()) {
            auto item = stack.top();
            stack.pop();
            auto id = get_id(item);
            if (visited.contains(id)) {
                continue;
            }

            auto result = std::visit(visitor, item);
            visited.insert(id);
            if (result == sm::visit_result::terminate_traversal) {
                return;
            } else if (result == sm::visit_result::terminate_branch) {
                continue;
            }

            auto neighbors = std::visit(neighbors_visitor, item);
            for (const auto& neighbor : neighbors) {
                stack.push(neighbor);
            }
        }
    }
}

/*------------------------------------------------------------------------------------------------*/

sm::skeleton::skeleton(world& w) :
    owner_(w) {

}

sm::skeleton::skeleton(world& w, const std::string& name, double x, double y) :
		owner_(w),
		name_(name),
		root_(w.create_node(*this, x, y)) {
	nodes_.insert({ root_->get().name(), &root_->get()});
}

void sm::skeleton::on_new_bone(sm::bone& b) {
	auto nodes = to_name_table<sm::node>(nodes_from_traversal(root_node()));
	auto bones = to_name_table<sm::bone>(bones_from_traversal(root_node()));

    auto iter = r::find_if(nodes,
            [this](const auto& pair)->bool {
                return pair.second != &root_node().get() && pair.first == "root";
            }
        );
    if (iter != nodes.end()) {
        auto* node = iter->second;
        nodes.erase(iter);
        nodes.insert({ "node-0", node });
    }

	nodes_.clear();
	bones_.clear();

	auto new_node_names = get_unique_names(nodes);
	for (const auto& [name, ptr] : new_node_names) {
		ptr->set_name(name);
		nodes_[name] = ptr;
	}

	auto new_bone_names = get_unique_names(bones);
	for (const auto& [name,ptr] : new_bone_names) {
		ptr->set_name(name);
		bones_[name] = ptr;
	}
}

std::string sm::skeleton::name() const {
	return name_;
}

void sm::skeleton::set_name(const std::string& str) {
	name_ = str;
}

sm::node_ref sm::skeleton::root_node() {
	return root_.value();
}

sm::const_node_ref sm::skeleton::root_node() const {
	return root_.value();
}

std::any sm::skeleton::get_user_data() const {
	return user_data_;
}

void sm::skeleton::set_user_data(std::any data) {
	user_data_ = data;
}

void sm::skeleton::clear_user_data() {
	user_data_.reset();
}

sm::expected_skel sm::skeleton::copy_to(world& other_world, const std::string& new_name) const {
    auto name = (new_name.empty()) ? name_ : new_name;
    auto new_skel = other_world.create_skeleton(name);
    if (!new_skel) {
        return new_skel;
    }
    for (auto node : nodes()) {
        auto new_node = node.get().copy_to(*new_skel);
        if (node.get().is_root()) {
            new_skel->get().set_root(new_node->get());
        }
    }
    for (auto bone : bones()) {
        bone.get().copy_to(*new_skel);
    }
    return new_skel;
}

sm::result sm::skeleton::set_name(bone& bone, const std::string& new_name) {
	if (bones_.contains(new_name)) {
		return result::non_unique_name;
	}
	auto old_name = bone.name();
	bone.set_name(new_name);
	bones_.erase(old_name);
	bones_[new_name] = &bone;

	return result::success;
}

sm::result sm::skeleton::set_name(node& node, const std::string& new_name) {
	if (nodes_.contains(new_name)) {
		return result::non_unique_name;
	}
	auto old_name = node.name();
	node.set_name(new_name);
	nodes_.erase(old_name);
	nodes_[new_name] = &node;

	return result::success;
}

sm::result sm::skeleton::from_json(sm::world& w, const json& jobj) {
    name_ = jobj["name"];
    nodes_ = jobj["nodes"] |
        rv::transform(
            [&](const json& jobj)->nodes_tbl::value_type {
                const auto& pos = jobj["pos"];
                auto new_node = w.create_node(
                    *this, jobj["name"], pos["x"].get<double>(), pos["y"].get<double>()
                );
                return { jobj["name"], &new_node.get() };
                }
        ) | r::to<nodes_tbl>();

    bones_ = jobj["bones"] |
        rv::transform(
            [&](const json& jobj)->bones_tbl::value_type {
                auto u = jobj["u"].get<std::string>();
                auto v = jobj["v"].get<std::string>();
                auto b = w.create_bone_in_skeleton(
                    jobj["name"].get<std::string>(),
                    *nodes_[u],
                    *nodes_[v]
                );
                if (jobj.contains("rot_constraint")) {
                    const auto& constraint = jobj["rot_constraint"];
                    b->get().set_rotation_constraint(
                        constraint["start_angle"].get<double>(),
                        constraint["span_angle"].get<double>(),
                        constraint["relative_to_parent"].get<bool>()
                    );
                }
                return { b->get().name(), &b->get() };
            }
        ) | r::to<bones_tbl>();

    root_ = *nodes_.at(jobj["root"]);

	return sm::result::success;
}

json sm::skeleton::to_json() const {
    auto nodes = nodes_ | rv::transform(
        [](const auto& pair)->const node& {
            return *pair.second;
        }
    ) | rv::transform(node_to_json) | r::to<json>();

    auto bones = bones_ | rv::transform(
        [](const auto& pair)->const bone& {
            return *pair.second;
        }
    ) | rv::transform(bone_to_json) | r::to<json>();

    return  {
        {"name", name_},
        {"nodes", nodes},
        {"bones", bones},
        {"root", root_node().get().name()}
    };
}

void sm::skeleton::set_root(sm::node& new_root)
{
    root_ = std::ref(new_root);
}

void sm::skeleton::set_owner(sm::world& owner)
{
    owner_ = owner;
}

void sm::skeleton::register_node(sm::node& new_node) {
    if (contains<node>(new_node.name()) || &new_node.owner().get() != this) {
        throw std::runtime_error("sm::skeleton::register_node failed");
    }
    nodes_[new_node.name()] = &new_node;
    if (!root_) {
        root_ = new_node;
    }
}

void sm::skeleton::register_bone(sm::bone& new_bone) {
    if (contains<node>(new_bone.name()) || &new_bone.owner().get() != this) {
        throw std::runtime_error("sm::skeleton::register_node failed");
    }
    bones_[new_bone.name()] = &new_bone;
}

bool sm::skeleton::empty() const {
    return !root_.has_value();
}

sm::world_ref sm::skeleton::owner() {
	return owner_;
}

sm::const_world_ref sm::skeleton::owner() const {
	return owner_;
}

void sm::skeleton::apply(matrix& mat) {
    for (auto node : nodes()) {
        node.get().apply(mat);
    }
}

/*------------------------------------------------------------------------------------------------*/

sm::world::world() {}

sm::world::world(sm::world&& other) {
    *this = std::move(other);
}

sm::world& sm::world::operator=(world&& other) {
    skeletons_ = std::move(other.skeletons_);
    bones_ = std::move(other.bones_);
    nodes_ = std::move(other.nodes_);

    for (auto& pair : skeletons_) {
        pair.second->set_owner(*this);
    }
    return *this;
}

void sm::world::clear() {
    skeletons_.clear();
    bones_.clear();
    nodes_.clear();
}

sm::skel_ref sm::world::create_skeleton(double x, double y) {
	auto new_name = unique_name("skeleton", skeleton_names());
	skeletons_.emplace( new_name, skeleton::make_unique( *this, new_name, x, y ) );
	return *skeletons_[new_name];
}

sm::skel_ref sm::world::create_skeleton(const point& pt) {
    return create_skeleton(pt.x, pt.y);
}

sm::expected_skel sm::world::create_skeleton(const std::string& name) {
    if (contains_skeleton_name(name)) {
        return std::unexpected(result::non_unique_name);
    }
    skeletons_.emplace(name, skeleton::make_unique(*this));
    auto& skel = *skeletons_[name];
    skel.set_name(name);

    return std::ref(skel);
}

sm::expected_skel sm::world::skeleton(const std::string& name) {
    auto const_this = const_cast<const world*>(this);
    auto skel = const_this->skeleton(name);
    if (!skel) {
        return std::unexpected(skel.error());
    }
    return std::ref(const_cast<sm::skeleton&>(skel->get()));
}

sm::expected_const_skel sm::world::skeleton(const std::string& name) const {
    auto iter = skeletons_.find(name);
    if (iter == skeletons_.end()) {
        return std::unexpected(sm::result::not_found);
    }
    return *iter->second;
}

template<typename T>
void delete_ptrs_if(std::vector<std::unique_ptr<T>>& vec, std::function<bool(const T&)> predicate) {
    vec.erase(
        std::remove_if(
            vec.begin(), vec.end(), 
            [&](const std::unique_ptr<T>& item) {
                return predicate(*item);
            }
        ), 
        vec.end()
    );
}

sm::result sm::world::delete_skeleton(const std::string& skel_name) {
    auto skel_ref = this->skeleton(skel_name);
    if (!skel_ref) {
        return sm::result::not_found;
    }
    auto& skel = skel_ref->get();

    auto bone_set = bones_ |
        rv::transform(
            [](const auto& bone_ptr)->bone* {
                return bone_ptr.get();
            }
        ) |  rv::filter(
            [&skel](const sm::bone* e)->bool {
                return &e->owner().get() == &skel;
            }
        ) | r::to<std::unordered_set<const bone*>>();

    auto node_set = nodes_ |
        rv::transform(
            [](const auto& node_ptr)->node* {
                return node_ptr.get();
            }
        ) | rv::filter(
            [&skel](const sm::node* n)->bool {
                return &n->owner().get() == &skel;
            }
        ) | r::to<std::unordered_set<const node*>>();

    delete_ptrs_if<sm::bone>(bones_,
        [&bone_set](const sm::bone& e)->bool {
            return bone_set.contains(&e);
        }
    );

    delete_ptrs_if<sm::node>(nodes_,
        [&node_set](const sm::node& v)->bool {
            return node_set.contains(&v);
        }
    );

    skeletons_.erase(skel_name); 
    return sm::result::success;
}

std::vector<std::string> sm::world::skeleton_names() const {
	return skeletons() |
		rv::transform([](auto skel) {return skel.get().name(); }) |
		r::to< std::vector<std::string>>();
}

bool sm::world::contains_skeleton_name(const std::string& name) const {
	return skeletons_.contains(name);
}

sm::result sm::world::set_name(sm::skeleton& skel, const std::string& new_name) {
	if (contains_skeleton_name(new_name)) {
		return result::non_unique_name;
	}

	auto old_name = skel.name();
	skel.set_name(new_name);
	skeletons_[new_name] = std::move(skeletons_[old_name]);
	skeletons_.erase(old_name);

	return result::success;
}

sm::node_ref sm::world::create_node(sm::skeleton& parent, const std::string& name, 
        double x, double y) {
    nodes_.push_back(node::make_unique(parent, name, x, y));
    return *nodes_.back();
}

sm::node_ref sm::world::create_node(sm::skeleton& parent, double x, double y) {
    return create_node(parent, "root", x, y);
}

sm::expected_bone sm::world::create_bone_in_skeleton(
        const std::string& bone_name, node & u, node& v) {
    if (!v.is_root()) {
        return std::unexpected(sm::result::multi_parent_node);
    }
    auto skel_u = u.owner();
    auto skel_v = v.owner();
    if (&skel_u.get() != &skel_v.get()) {
        return std::unexpected(sm::result::cross_skeleton_bone);
    }
    bones_.push_back(bone::make_unique(bone_name, u, v));
    return *bones_.back();
}

std::expected<sm::bone_ref, sm::result> sm::world::create_bone(
        const std::string& bone_name, node& u, node& v) {

    if (!v.is_root()) {
        return std::unexpected(sm::result::multi_parent_node);
    }

	auto skel_u = u.owner();
	auto skel_v = v.owner();
    if (&skel_u.get() == &skel_v.get()) {
        return std::unexpected(sm::result::cyclic_bones);
    }

	skeletons_.erase(skel_v.get().name());

    std::string name = (bone_name.empty()) ? "bone-1" : bone_name;
	bones_.push_back(bone::make_unique("bone-1", u, v));
	skel_u.get().on_new_bone( *bones_.back() );

    return *bones_.back();
}

sm::result sm::world::from_json_str(const std::string& str) {
    try {
        auto js = json::parse(str);
        return from_json(js);
    } catch (...) {
        return sm::result::invalid_json;
    }
    return sm::result::success;
}

sm::result sm::world::from_json(const json& stick_man)
{
	try {
		skeletons_ = stick_man["skeletons"] |
			rv::transform(
				[this](const json& jobj)->skeleton_tbl::value_type {
                    std::unique_ptr<sm::skeleton> skel = skeleton::make_unique(*this);
                    skel->from_json(*this, jobj);
                    return { skel->name(), std::move(skel) };
				}
		) | r::to<skeleton_tbl>();
	}
	catch (...) {
		return sm::result::invalid_json;
	}
	return sm::result::success;
}

std::string sm::world::to_json_str() const {
    return to_json().dump(4);
}

json sm::world::to_json() const {
    auto skeleton_json = skeletons_ | rv::transform(
        [](const auto& pair)->const sm::skeleton* {
            return pair.second.get();
        }
    ) | rv::transform(&sm::skeleton::to_json) | r::to<json>();

    json stick_man = {
        {"version", 0.0},
        {"skeletons", skeleton_json}
    };
    return stick_man;
}

void sm::world::apply(matrix& mat) {
    for (auto skel : skeletons()) {
        skel.get().apply(mat);
    }
}

void sm::dfs(node& root, node_visitor visit_node, bone_visitor visit_bone,
        bool just_downstream) {
    dfs_aux<sm::node,sm::bone>(std::ref(root), visit_node, visit_bone, just_downstream);
}

void sm::dfs(bone& root, node_visitor visit_node, bone_visitor visit_bone,
    bool just_downstream) {
    dfs_aux<sm::node, sm::bone>(std::ref(root), visit_node, visit_bone, just_downstream);
}

void sm::dfs(const node& root, const_node_visitor visit_node,
        const_bone_visitor visit_bone, bool just_downstream) {
    dfs_aux<const sm::node, const sm::bone>(std::ref(root), visit_node, visit_bone, just_downstream);
}

void sm::dfs(const bone& root, const_node_visitor visit_node,
        const_bone_visitor visit_bone, bool just_downstream) {
    dfs_aux<const sm::node, const sm::bone>(std::ref(root), visit_node, visit_bone, just_downstream);
}

void sm::visit_nodes(node& j, node_visitor visit_node) {
    dfs(j, visit_node, {}, true);
}

void sm::visit_bones(node& j, bone_visitor visit_bone) {
	dfs(j, {}, visit_bone, true);
}

void sm::visit_bones(bone& b, bone_visitor visit_bone) {
	visit_bone(b);
	visit_bones(b.child_node(), visit_bone);
}

sm::fabrik_options::fabrik_options() :
	max_iterations{k_max_iter},
	tolerance{k_tolerance},
	forw_reaching_constraints{false},
	max_ang_delta{0.0}
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

double constrain_rotation(sm::bone& b, double theta) {

	fabrik_item fi(b);
	return ::apply_rotation_constraints(fi, theta).value_or(theta);
}


