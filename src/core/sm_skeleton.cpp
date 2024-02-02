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

        std::vector<sm::node_or_bone<V,E>> operator()(std::reference_wrapper<V> j_ref) {
            auto& node = j_ref.get();
            auto neighbors = node.child_bones() |
                rv::transform(
                    [](auto child)->sm::node_or_bone<V, E> {
                        return child;
                    }
                ) | r::to<std::vector<sm::node_or_bone<V,E>>>();
            if (!downstream_) {
                if (node.parent_bone().has_value()) {
                    neighbors.push_back(node.parent_bone().value());
                }
            }
            return neighbors;
        }

        std::vector<sm::node_or_bone<V, E>> operator()(std::reference_wrapper<E> b_ref) {
            auto& bone = b_ref.get();
            std::vector<sm::node_or_bone<V, E>> neighbors;
            if (!downstream_) {
                neighbors.push_back(std::ref(bone.parent_node()));
            }
            neighbors.push_back(std::ref(bone.child_node()));
            return neighbors;
        }
    };

    template<typename V, typename E>
    uint64_t get_id(const sm::node_or_bone<V,E>& var) {
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
	struct stack_item {
		std::optional<std::reference_wrapper<V>> prev;
        sm::node_or_bone<V, E> val;
	};

	template<typename V, typename E>
	std::reference_wrapper<V> get_prev(stack_item<V, E> item) {
		return std::visit(
			overloaded{
				[](std::reference_wrapper<V> j_ref) -> std::reference_wrapper<V> {
					return j_ref;
				},
				[](std::reference_wrapper<E> b_ref) -> std::reference_wrapper<V> {
					return b_ref.get().parent_node();
				}
			}, 
			item.val
		);
	}
	
    template<typename V, typename E>
    void dfs_aux(sm::node_or_bone<V,E> root, visitor_fn<V> visit_node, visitor_fn<E> visit_bone,
            bool just_downstream) {
		using item_t = stack_item<V, E>;
        std::stack<item_t> stack;
        std::unordered_set<uint64_t> visited;
        node_or_bone_visitor<V,E> visitor(visit_node, visit_bone);
        node_or_bone_neighbors_visitor<V,E> neighbors_visitor(just_downstream);
        stack.push(item_t{std::nullopt,root});

        while (!stack.empty()) {
            auto item = stack.top();
            stack.pop();
			auto id = get_id(item.val);
            if (visited.contains(id)) {
                continue;
            }

            auto result = std::visit(visitor, item.val);
            visited.insert(id);
            if (result == sm::visit_result::terminate_traversal) {
                return;
            } else if (result == sm::visit_result::terminate_branch) {
                continue;
            }

            auto neighbors = std::visit(neighbors_visitor, item.val);
            for (const auto& neighbor : neighbors) {
				std::optional<std::reference_wrapper<V>> prev = { get_prev<V, E>(item) };
				stack.push(item_t{ prev, neighbor });
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
                return pair.second != &root_node() && pair.first == "root";
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

sm::node& sm::skeleton::root_node() {
	return root_.value();
}

const sm::node& sm::skeleton::root_node() const {
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
        {"root", root_node().name()}
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
    if (contains<node>(new_node.name()) || &new_node.owner() != this) {
        throw std::runtime_error("sm::skeleton::register_node failed");
    }
    nodes_[new_node.name()] = &new_node;
    if (!root_) {
        root_ = new_node;
    }
}

void sm::skeleton::register_bone(sm::bone& new_bone) {
    if (contains<node>(new_bone.name()) || &new_bone.owner() != this) {
        throw std::runtime_error("sm::skeleton::register_node failed");
    }
    bones_[new_bone.name()] = &new_bone;
}

bool sm::skeleton::empty() const {
    return !root_.has_value();
}

sm::world& sm::skeleton::owner() {
	return owner_;
}

const sm::world& sm::skeleton::owner() const {
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

bool sm::world::empty() const {
    return skeletons_.empty();
}

sm::skeleton& sm::world::create_skeleton(double x, double y) {
	auto new_name = unique_name("skeleton", skeleton_names());
	skeletons_.emplace( new_name, skeleton::make_unique( *this, new_name, x, y ) );
	return *skeletons_[new_name];
}

sm::skeleton& sm::world::create_skeleton(const point& pt) {
    return create_skeleton(pt.x, pt.y);
}

sm::expected_skel sm::world::create_skeleton(const std::string& name) {
    if (contains_skeleton(name)) {
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
                return &e->owner() == &skel;
            }
        ) | r::to<std::unordered_set<const bone*>>();

    auto node_set = nodes_ |
        rv::transform(
            [](const auto& node_ptr)->node* {
                return node_ptr.get();
            }
        ) | rv::filter(
            [&skel](const sm::node* n)->bool {
                return &n->owner() == &skel;
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

bool sm::world::contains_skeleton(const std::string& name) const {
	return skeletons_.contains(name);
}

sm::result sm::world::set_name(sm::skeleton& skel, const std::string& new_name) {
	if (contains_skeleton(new_name)) {
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
    auto& skel_u = u.owner();
    auto& skel_v = v.owner();
    if (&skel_u != &skel_v) {
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

	auto& skel_u = u.owner();
	auto& skel_v = v.owner();
    if (&skel_u == &skel_v) {
        return std::unexpected(sm::result::cyclic_bones);
    }

	skeletons_.erase(skel_v.name());

    std::string name = (bone_name.empty()) ? "bone-1" : bone_name;
	bones_.push_back(bone::make_unique("bone-1", u, v));
	skel_u.on_new_bone( *bones_.back() );

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
    dfs_aux<sm::node,sm::bone>(
		std::ref(root), visit_node, visit_bone, just_downstream
	);
}

void sm::dfs(bone& root, node_visitor visit_node, bone_visitor visit_bone,
    bool just_downstream) {
    dfs_aux<sm::node, sm::bone>(
		std::ref(root), visit_node, visit_bone, just_downstream
	);
}

void sm::dfs(const node& root, const_node_visitor visit_node,
        const_bone_visitor visit_bone, bool just_downstream) {
    dfs_aux<const sm::node, const sm::bone>(
		std::ref(root), visit_node, visit_bone, just_downstream
	);
}

void sm::dfs(const bone& root, const_node_visitor visit_node,
        const_bone_visitor visit_bone, bool just_downstream) {
    dfs_aux<const sm::node, const sm::bone>(
		std::ref(root), visit_node, visit_bone, just_downstream
	);
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



