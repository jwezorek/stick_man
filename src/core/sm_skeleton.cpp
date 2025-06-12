#include "sm_skeleton.h"
#include "sm_world.h"
#include "sm_types.h"
#include "sm_visit.h"
#include "sm_animation.h"
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

	template<typename T>
	auto to_name_table(auto itms) {
		using name_table_t = std::multimap<std::string, T*>;
		return itms | rv::transform(
				[](auto r)->name_table_t::value_type {
					return { r->name(), r.ptr() };
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
	std::vector<std::pair<std::string,T*>> get_unique_names(
            const std::multimap<std::string, T*>& tbl) {
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
        auto new_node = node->copy_to(*new_skel);
        if (node->is_root()) {
            new_skel->get().set_root(new_node->get());
        }
    }
    for (auto bone : bones()) {
        auto result = bone->copy_to(*new_skel);
        if (!result) {
            return std::unexpected(result.error());
        }
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
                return { jobj["name"], new_node.ptr() };
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

    if (jobj.contains("animations")) {
        for (const auto& anim_json : jobj["animations"]) {
            sm::animation anim(anim_json["name"]);
            const auto& events_json = anim_json["events"];

            for (const auto& ev : events_json) {
                animation_event event;
                event.start_time = ev["start_time"];
                event.duration = ev["duration"];
                event.index = ev.contains("index") ? ev["index"].get<int>() : 0;
                std::string type = ev["type"];

                if (type == "rotate") {
                    event.action = sm::rotation{
                        .axis = ev["axis"].get<std::string>(),
                        .rotor = ev["rotor"].get<std::string>(),
                        .theta = ev["angle"].get<double>()
                    };
                } else if (type == "translate") {
                    // Stub implementation
                    event.action = sm::translation{
                        .subject = ev["subject"].get<std::string>(),
                        .offset = sm::point{0.0, 0.0} // Placeholder
                    };
                }
                else {
                    throw std::runtime_error("Unknown animation event type: " + type);
                }

                anim.insert(event);
            }

            insert_animation(anim);
        }
    }

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
    root_ = sm::ref(new_root);
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

sm::pose sm::skeleton::current_pose() const {
    return nodes_ | rv::values | rv::transform(
        [](auto* node) {
            return node->world_pos();
        }
    ) | r::to<sm::pose>();
}

void sm::skeleton::set_pose(const pose& pose) {
    if (pose.size() != nodes_.size()) {
        throw std::runtime_error("sm::skeleton: pose size mismatch");
    }
    for (auto [node, position] : rv::zip(nodes_ | rv::values, pose)) {
        node->set_world_pos(position);
    }
}

const std::vector<sm::animation>& sm::skeleton::animations() const {
    return animations_;
}

const sm::animation& sm::skeleton::get_animation(const std::string& anim) const {
    return *r::find_if(animations_, [&](const auto& a) {return a.name() == anim; });
}

void sm::skeleton::insert_animation(const animation& anim) {
    animations_.push_back( anim );
}

sm::world& sm::skeleton::owner() {
	return owner_;
}

const sm::world& sm::skeleton::owner() const {
	return owner_;
}

void sm::skeleton::apply(matrix& mat) {
    for (auto node : nodes()) {
        node->apply(mat);
    }
}

/*------------------------------------------------------------------------------------------------*/

std::vector<sm::skel_ref> sm::skeletons_from_nodes(const std::vector<sm::node_ref>& nodes) {
    auto skels = nodes |
        rv::transform(
            [](auto node) {return &node->owner(); }
        ) | r::to<std::unordered_set>();

    return skels | rv::transform(
        [](auto* ptr)->sm::skel_ref {
            return *ptr;
        }
    ) | r::to<std::vector>();
}

