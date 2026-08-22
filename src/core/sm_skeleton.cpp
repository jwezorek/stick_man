#include "sm_skeleton.h"
#include "sm_types.h"
#include "sm_visit.h"
#include "sm_animation.h"
#include "json.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <functional>
#include <limits>
#include <numbers>
#include <ranges>
#include <stack>
#include <tuple>
#include <unordered_map>
#include <unordered_set>

using namespace std::placeholders;

namespace r = std::ranges;
namespace rv = std::ranges::views;
using json = nlohmann::json;
/*------------------------------------------------------------------------------------------------*/

namespace {

    bool is_prefix(const std::string& prefix, const std::string& str) {
        auto [lhs, rhs] = r::mismatch(prefix, str);
        return lhs == prefix.end();
    }
    std::vector<int> extract_prefixed_integers(
        const std::string& prefix, const std::vector<std::string>& names) {
        auto n = prefix.size();
        return names | rv::filter(
            std::bind(is_prefix, prefix, _1)
        ) | rv::transform(
            [n](const auto& str)->int {
                try {
                    auto num_str = str.substr(n, str.size() - n);
                    return std::stoi(num_str);
                }
                catch (...) {
                    return 0;
                }
            }
        ) | rv::filter(
            [](int v) { return v > 0; }
        ) | r::to<std::vector<int>>();
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
        return static_cast<int>(std::distance(appears.begin(), first_false));
    }
    std::string unique_name(const std::string& prefix, const std::vector<std::string>& names) {
        std::string prefix_with_hyphen = prefix + "-";
        auto ids_taken = extract_prefixed_integers(prefix_with_hyphen, names);
        auto index = smallest_excluded_positive_integer(ids_taken);
        return prefix + "-" + std::to_string(index);
    }
    sm::object_id parsed_id(const json& obj, const char* key) {
        if (!obj.contains(key)) {
            return sm::object_id::generate();
        }
        auto parsed = sm::object_id::from_string(obj.at(key).get<std::string>());
        if (!parsed) {
            throw std::runtime_error(parsed.error());
        }
        return *parsed;
    }
    sm::node* node_from_reference(sm::skeleton& skel, const json& ref) {
        auto str = ref.get<std::string>();
        if (auto id = sm::object_id::from_string(str); id) {
            if (auto node = skel.get<sm::node>(*id)) {
                return node->ptr();
            }
        }
        // Legacy files used names as references. Names were unique in that format.
        if (auto node = skel.get_by_name<sm::node>(str)) {
            return node->ptr();
        }
        return nullptr;
    }
    json node_to_json(const sm::node& node) {
        return {
            {"id", node.id().to_string()},
            {"name", node.name()},
            {"pos", {{"x", node.world_x()}, {"y", node.world_y()}}}
        };
    }
    json bone_to_json(const sm::bone& bone) {
        json bone_json = {
            {"id", bone.id().to_string()},
            {"name", bone.name()},
            {"u", bone.parent_node().id().to_string()},
            {"v", bone.child_node().id().to_string()}
        };
        if (auto constraint = bone.rotation_constraint()) {
            bone_json["rot_constraint"] = {
                {"relative_to_parent", constraint->relative_to_parent},
                {"start_angle", constraint->start_angle},
                {"span_angle", constraint->span_angle}
            };
        }
        return bone_json;
    }
    json animation_event_to_json(const sm::animation_event& event) {
        return std::visit(sm::overloaded{
            [](const sm::rotation& rotation)->json {
                return {
                    {"type", "rotation"},
                    {"axis", rotation.axis.to_string()},
                    {"rotor", rotation.rotor.to_string()},
                    {"theta", rotation.theta},
                    {"duration", rotation.duration}
                };
            },
            [](const sm::translation& translation)->json {
                return {
                    {"type", "translation"},
                    {"subject", translation.subject.to_string()},
                    {"offset", {{"x", translation.offset.x}, {"y", translation.offset.y}}},
                    {"duration", translation.duration}
                };
            }
            }, event);
    }
    sm::animation_event animation_event_from_json(const json& event) {
        auto type = event.at("type").get<std::string>();
        if (type == "rotation") {
            auto axis = sm::object_id::from_string(event.at("axis").get<std::string>());
            auto rotor = sm::object_id::from_string(event.at("rotor").get<std::string>());
            if (!axis || !rotor) {
                throw std::runtime_error("invalid animation object ID");
            }
            return sm::rotation{
                *axis, *rotor,
                event.at("theta").get<double>(),
                event.at("duration").get<int>()
            };
        }
        if (type == "translation") {
            auto subject = sm::object_id::from_string(event.at("subject").get<std::string>());
            if (!subject) {
                throw std::runtime_error("invalid animation object ID");
            }
            const auto& offset = event.at("offset");
            return sm::translation{
                *subject,
                {offset.at("x").get<double>(), offset.at("y").get<double>()},
                event.at("duration").get<int>()
            };
        }
        throw std::runtime_error("unknown animation event type");
    }
    sm::animation_event remap_event(
        const sm::animation_event& event,
        const std::unordered_map<sm::object_id, sm::object_id>& ids) {
        auto remap = [&ids](const sm::object_id& id) {
            auto it = ids.find(id);
            return it == ids.end() ? id : it->second;
            };
        return std::visit(sm::overloaded{
            [&](const sm::rotation& rotation)->sm::animation_event {
                return sm::rotation{
                    remap(rotation.axis), remap(rotation.rotor),
                    rotation.theta, rotation.duration
                };
            },
            [&](const sm::translation& translation)->sm::animation_event {
                return sm::translation{
                    remap(translation.subject), translation.offset, translation.duration
                };
            }
            }, event);
    }
}

/*------------------------------------------------------------------------------------------------*/

sm::skeleton::skeleton(world& w, object_id id) :
    id_(id), owner_(w) {}

sm::skeleton::skeleton(world& w, object_id id, const std::string& name, double x, double y) :
    id_(id), owner_(w), name_(name) {
    auto root = w.create_node(*this, x, y);
    register_node(root);
}
void sm::skeleton::on_new_bone(sm::bone& b) {
    nodes_.clear();
    bones_.clear();
    sm::visit_nodes_and_bones(
        root_node(),
        [this](sm::node& node) {
            nodes_[node.id()] = &node;
            return sm::visit_result::continue_traversal;
        },
        [this](sm::bone& bone) {
            bones_[bone.id()] = &bone;
            return sm::visit_result::continue_traversal;
        },
        true
    );
}
const sm::object_id& sm::skeleton::id() const noexcept { return id_; }

std::string sm::skeleton::name() const { return name_; }

void sm::skeleton::set_name(const std::string& str) { name_ = str; }

sm::node& sm::skeleton::root_node() { return root_.value(); }
const sm::node& sm::skeleton::root_node() const { return root_.value(); }
std::any sm::skeleton::get_user_data() const { return user_data_; }
void sm::skeleton::set_user_data(std::any data) { user_data_ = data; }
void sm::skeleton::clear_user_data() { user_data_.reset(); }

sm::expected_skel sm::skeleton::copy_to(world& other_world, const std::string& new_name) const {
    auto label = new_name.empty() ? name_ : new_name;
    auto new_skel = other_world.create_skeleton_with_id(id_, label);
    if (!new_skel) {
        return new_skel;
    }
    auto& dest = new_skel->get();
    for (auto node : nodes()) {
        auto copied = node->copy_to(dest);
        if (!copied) {
            return std::unexpected(copied.error());
        }
    }
    dest.set_root(dest.get<sm::node>(root_node().id())->get());
    for (auto bone : bones()) {
        auto copied = bone->copy_to(dest);
        if (!copied) {
            return std::unexpected(copied.error());
        }
    }
    dest.animations_.reserve(animations_.size());
    for (const auto& anim : animations_) {
        dest.animations_.push_back(anim);
    }
    dest.user_data_ = user_data_;
    return new_skel;
}
sm::expected_skel sm::skeleton::duplicate_to(world& other_world, const std::string& new_name) const {
    auto label = new_name.empty() ? name_ : new_name;
    auto new_skel = other_world.create_skeleton(label);
    if (!new_skel) {
        return new_skel;
    }

    auto& dest = new_skel->get();
    std::unordered_map<object_id, object_id> id_map;
    id_map.emplace(id_, dest.id());
    for (auto node : nodes()) {
        auto new_id = object_id::generate();
        id_map.emplace(node->id(), new_id);
        auto copied = other_world.create_node(
            dest, new_id, node->name(), node->world_x(), node->world_y());
        dest.register_node(copied);
    }
    dest.set_root(dest.get<sm::node>(id_map.at(root_node().id()))->get());
    for (auto bone : bones()) {
        auto new_id = object_id::generate();
        id_map.emplace(bone->id(), new_id);
        auto u = dest.get<sm::node>(id_map.at(bone->parent_node().id()));
        auto v = dest.get<sm::node>(id_map.at(bone->child_node().id()));
        auto copied = other_world.create_bone_in_skeleton(new_id, bone->name(), u->get(), v->get());
        if (!copied) {
            return std::unexpected(copied.error());
        }
        if (auto constraint = bone->rotation_constraint()) {
            copied->get().set_rotation_constraint(
                constraint->start_angle,
                constraint->span_angle,
                constraint->relative_to_parent);
        }
        dest.register_bone(copied->get());
    }
    for (const auto& anim : animations_) {
        animation new_anim(object_id::generate(), anim.name_);
        id_map.emplace(anim.id_, new_anim.id_);
        for (const auto& [time, events] : anim.timeline_) {
            for (const auto& event : events) {
                new_anim.insert(time, remap_event(event, id_map));
            }
        }
        dest.animations_.push_back(std::move(new_anim));
    }
    return new_skel;
}
void sm::skeleton::set_name(bone& bone, const std::string& new_name) {
    bone.set_name(new_name);
}

void sm::skeleton::set_name(node& node, const std::string& new_name) {
    node.set_name(new_name);
}

sm::result sm::skeleton::from_json(sm::world& w, const json& jobj) {
    name_ = jobj.at("name").get<std::string>();
    nodes_.clear();
    bones_.clear();
    animations_.clear();
    for (const auto& node_json : jobj.at("nodes")) {
        const auto& pos = node_json.at("pos");
        auto new_node = w.create_node(
            *this,
            parsed_id(node_json, "id"),
            node_json.at("name").get<std::string>(),
            pos.at("x").get<double>(),
            pos.at("y").get<double>());
        if (nodes_.contains(new_node->id())) {
            return sm::result::invalid_json;
        }
        nodes_[new_node->id()] = new_node.ptr();
    }
    for (const auto& bone_json : jobj.at("bones")) {
        auto* u = node_from_reference(*this, bone_json.at("u"));
        auto* v = node_from_reference(*this, bone_json.at("v"));
        if (!u || !v) {
            return sm::result::invalid_json;
        }
        auto b = w.create_bone_in_skeleton(
            parsed_id(bone_json, "id"),
            bone_json.at("name").get<std::string>(), *u, *v);
        if (!b || bones_.contains(b->get().id())) {
            return sm::result::invalid_json;
        }
        if (bone_json.contains("rot_constraint")) {
            const auto& constraint = bone_json.at("rot_constraint");
            b->get().set_rotation_constraint(
                constraint.at("start_angle").get<double>(),
                constraint.at("span_angle").get<double>(),
                constraint.at("relative_to_parent").get<bool>());
        }
        bones_[b->get().id()] = &b->get();
    }
    auto* root = node_from_reference(*this, jobj.at("root"));
    if (!root) {
        return sm::result::invalid_json;
    }
    root_ = *root;
    if (jobj.contains("animations")) {
        for (const auto& anim_json : jobj.at("animations")) {
            animation anim(parsed_id(anim_json, "id"), anim_json.at("name").get<std::string>());
            for (const auto& entry : anim_json.at("timeline")) {
                auto time = entry.at("start_time").get<int>();
                for (const auto& event : entry.at("events")) {
                    anim.insert(time, animation_event_from_json(event));
                }
            }
            animations_.push_back(std::move(anim));
        }
    }
    return sm::result::success;
}

json sm::skeleton::to_json() const {
    json nodes = json::array();
    for (auto node : this->nodes()) {
        nodes.push_back(node_to_json(*node));
    }

    json bones = json::array();
    for (auto bone : this->bones()) {
        bones.push_back(bone_to_json(*bone));
    }
    json animations = json::array();
    for (const auto& anim : animations_) {
        json timeline = json::array();
        for (const auto& [time, events] : anim.timeline_) {
            json event_json = json::array();
            for (const auto& event : events) {
                event_json.push_back(animation_event_to_json(event));
            }
            timeline.push_back({ {"start_time", time}, {"events", event_json} });
        }
        animations.push_back({
            {"id", anim.id().to_string()},
            {"name", anim.name()},
            {"timeline", timeline}
            });
    }
    return {
        {"id", id_.to_string()},
        {"name", name_},
        {"nodes", nodes},
        {"bones", bones},
        {"root", root_node().id().to_string()},
        {"animations", animations}
    };
}

void sm::skeleton::set_root(sm::node& new_root) { root_ = sm::ref(new_root); }
void sm::skeleton::set_owner(sm::world& owner) { owner_ = owner; }
void sm::skeleton::register_node(sm::node& new_node) {
    if (nodes_.contains(new_node.id()) || &new_node.owner() != this) {
        throw std::runtime_error("sm::skeleton::register_node failed");
    }
    nodes_[new_node.id()] = &new_node;
    if (!root_) {
        root_ = new_node;
    }
}
void sm::skeleton::register_bone(sm::bone& new_bone) {
    if (bones_.contains(new_bone.id()) || &new_bone.owner() != this) {
        throw std::runtime_error("sm::skeleton::register_bone failed");
    }
    bones_[new_bone.id()] = &new_bone;
}
bool sm::skeleton::empty() const { return !root_.has_value(); }
const std::vector<sm::animation>& sm::skeleton::animations() const { return animations_; }
void sm::skeleton::insert_animation(const animation& anim) { animations_.push_back(anim); }
sm::world& sm::skeleton::owner() { return owner_; }
const sm::world& sm::skeleton::owner() const { return owner_; }

void sm::skeleton::apply(matrix& mat) {
    for (auto node : nodes()) {
        node->apply(mat);
    }
}
/*------------------------------------------------------------------------------------------------*/

sm::world::world() {}

sm::world::world(sm::world&& other) { *this = std::move(other); }

sm::world& sm::world::operator=(world&& other) {
    skeletons_ = std::move(other.skeletons_);
    bones_ = std::move(other.bones_);
    nodes_ = std::move(other.nodes_);
    for (auto& [id, skel] : skeletons_) {
        skel->set_owner(*this);
    }
    return *this;
}
void sm::world::clear() {
    skeletons_.clear();
    bones_.clear();
    nodes_.clear();
}

bool sm::world::empty() const { return skeletons_.empty(); }
sm::skeleton& sm::world::create_skeleton(double x, double y) {
    auto new_name = unique_name("skeleton", skeleton_names());
    auto id = object_id::generate();
    auto [it, inserted] = skeletons_.emplace(
        id, skeleton::make_unique(*this, id, new_name, x, y));
    if (!inserted) {
        throw std::runtime_error("generated duplicate object ID");
    }
    return *it->second;
}

sm::skeleton& sm::world::create_skeleton(const point& pt) { return create_skeleton(pt.x, pt.y); }
sm::expected_skel sm::world::create_skeleton_with_id(object_id id, const std::string& name) {
    if (skeletons_.contains(id)) {
        return std::unexpected(result::duplicate_id);
    }
    auto [it, inserted] = skeletons_.emplace(id, skeleton::make_unique(*this, id));
    if (!inserted) {
        return std::unexpected(result::duplicate_id);
    }
    it->second->set_name(name);
    return sm::ref(*it->second);
}
sm::expected_skel sm::world::create_skeleton(const std::string& name) {
    return create_skeleton_with_id(object_id::generate(), name);
}

sm::expected_skel sm::world::skeleton(const object_id& id) {
    auto const_this = const_cast<const world*>(this);
    auto skel = const_this->skeleton(id);
    if (!skel) {
        return std::unexpected(skel.error());
    }
    return sm::ref(const_cast<sm::skeleton&>(skel->get()));
}
sm::expected_const_skel sm::world::skeleton(const object_id& id) const {
    auto iter = skeletons_.find(id);
    if (iter == skeletons_.end()) {
        return std::unexpected(sm::result::not_found);
    }
    return *iter->second;
}
sm::expected_skel sm::world::skeleton(const std::string& name) {
    auto const_this = const_cast<const world*>(this);
    auto skel = const_this->skeleton(name);
    if (!skel) {
        return std::unexpected(skel.error());
    }
    return sm::ref(const_cast<sm::skeleton&>(skel->get()));
}
sm::expected_const_skel sm::world::skeleton(const std::string& name) const {
    // Label lookup is for display/legacy compatibility only. Names are not identity.
    for (const auto& [id, skel] : skeletons_) {
        if (skel->name() == name) {
            return *skel;
        }
    }
    return std::unexpected(sm::result::not_found);
}
template<typename T>
void delete_ptrs_if(std::vector<std::unique_ptr<T>>& vec, std::function<bool(const T&)> predicate) {
    vec.erase(std::remove_if(vec.begin(), vec.end(),
        [&](const std::unique_ptr<T>& item) { return predicate(*item); }), vec.end());
}

sm::result sm::world::delete_skeleton(const object_id& id) {
    auto skel_ref = skeleton(id);
    if (!skel_ref) {
        return sm::result::not_found;
    }
    auto& skel = skel_ref->get();
    // Capture membership before destroying anything.  node::owner() can follow a
    // parent bone, so asking a node for its owner after its parent bone has been
    // deleted dereferences a dangling bone_ref.  The skeleton's tables already
    // contain exactly the nodes and bones that belong to it, so use pointer
    // identity while erasing the owning vectors instead of recomputing ownership.
    std::unordered_set<const sm::bone*> bones_to_delete;
    bones_to_delete.reserve(skel.bones_.size());
    for (const auto& [bone_id, bone] : skel.bones_) {
        bones_to_delete.insert(bone);
    }
    std::unordered_set<const sm::node*> nodes_to_delete;
    nodes_to_delete.reserve(skel.nodes_.size());
    for (const auto& [node_id, node] : skel.nodes_) {
        nodes_to_delete.insert(node);
    }

    bones_.erase(std::remove_if(bones_.begin(), bones_.end(),
        [&bones_to_delete](const std::unique_ptr<sm::bone>& bone) {
            return bones_to_delete.contains(bone.get());
        }), bones_.end());
    nodes_.erase(std::remove_if(nodes_.begin(), nodes_.end(),
        [&nodes_to_delete](const std::unique_ptr<sm::node>& node) {
            return nodes_to_delete.contains(node.get());
        }), nodes_.end());

    skeletons_.erase(id);
    return sm::result::success;
}

std::vector<std::string> sm::world::skeleton_names() const {
    return skeletons() |
        rv::transform([](auto skel) { return skel->name(); }) |
        r::to<std::vector<std::string>>();
}
bool sm::world::contains_skeleton(const object_id& id) const { return skeletons_.contains(id); }

bool sm::world::contains_skeleton(const std::string& name) const {
    return r::any_of(skeletons_, [&](const auto& pair) { return pair.second->name() == name; });
}

void sm::world::set_name(sm::skeleton& skel, const std::string& new_name) {
    skel.set_name(new_name);
}
sm::node_ref sm::world::create_node(sm::skeleton& parent, object_id id,
    const std::string& name, double x, double y) {
    if (parent.contains<node>(id)) {
        throw std::runtime_error("duplicate node object ID");
    }
    nodes_.push_back(node::make_unique(parent, id, name, x, y));
    return *nodes_.back();
}

sm::node_ref sm::world::create_node(sm::skeleton& parent, const std::string& name,
    double x, double y) {
    return create_node(parent, object_id::generate(), name, x, y);
}
sm::node_ref sm::world::create_node(sm::skeleton& parent, double x, double y) {
    return create_node(parent, "root", x, y);
}
sm::expected_bone sm::world::create_bone_in_skeleton(
    object_id id, const std::string& bone_name, node& u, node& v) {
    if (!v.is_root()) {
        return std::unexpected(sm::result::multi_parent_node);
    }
    auto& skel_u = u.owner();
    auto& skel_v = v.owner();
    if (&skel_u != &skel_v) {
        return std::unexpected(sm::result::cross_skeleton_bone);
    }
    if (skel_u.contains<bone>(id)) {
        return std::unexpected(sm::result::duplicate_id);
    }
    bones_.push_back(bone::make_unique(id, bone_name, u, v));
    return *bones_.back();
}
sm::expected_bone sm::world::create_bone_in_skeleton(
    const std::string& bone_name, node& u, node& v) {
    return create_bone_in_skeleton(object_id::generate(), bone_name, u, v);
}

sm::expected_bone sm::world::create_bone(const std::string& bone_name, node& u, node& v) {
    return create_bone(object_id::generate(), bone_name, u, v);
}
sm::expected_bone sm::world::create_bone(object_id id, const std::string& bone_name, node& u, node& v) {
    if (!v.is_root()) {
        return std::unexpected(sm::result::multi_parent_node);
    }

    auto& skel_u = u.owner();
    auto& skel_v = v.owner();
    if (&skel_u == &skel_v) {
        return std::unexpected(sm::result::cyclic_bones);
    }
    if (skel_u.contains<bone>(id) || skel_v.contains<bone>(id)) {
        return std::unexpected(sm::result::duplicate_id);
    }
    skeletons_.erase(skel_v.id());
    bones_.push_back(bone::make_unique(id, bone_name, u, v));
    skel_u.on_new_bone(*bones_.back());
    return *bones_.back();
}

sm::result sm::world::from_json_str(const std::string& str) {
    try {
        return from_json(json::parse(str));
    }
    catch (...) {
        return sm::result::invalid_json;
    }
}
sm::result sm::world::from_json(const json& stick_man) {
    try {
        clear();
        for (const auto& jobj : stick_man.at("skeletons")) {
            auto id = parsed_id(jobj, "id");
            if (skeletons_.contains(id)) {
                return sm::result::invalid_json;
            }
            auto skel = skeleton::make_unique(*this, id);
            auto result = skel->from_json(*this, jobj);
            if (result != sm::result::success) {
                clear();
                return result;
            }
            skeletons_.emplace(id, std::move(skel));
        }
    }
    catch (...) {
        clear();
        return sm::result::invalid_json;
    }
    return sm::result::success;
}
std::string sm::world::to_json_str() const { return to_json().dump(4); }

json sm::world::to_json() const {
    json skeleton_json = json::array();
    for (auto skel : skeletons()) {
        skeleton_json.push_back(skel->to_json());
    }
    return { {"version", 1.0}, {"skeletons", skeleton_json} };
}

void sm::world::apply(matrix& mat) {
    for (auto skel : skeletons()) {
        skel->apply(mat);
    }
}
