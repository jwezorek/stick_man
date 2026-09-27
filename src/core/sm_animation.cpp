#include "sm_animation.hpp"
#include "sm_geometry_batch.hpp"
#include "sm_skeleton.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <unordered_set>

const sm::pose* sm::animation_assets::find_pose(object_id id) const {
    for (const auto& p : poses) if (p.id == id) return &p;
    return nullptr;
}

const sm::animation* sm::animation_assets::find_animation(object_id id) const {
    for (const auto& a : animations) if (a.id == id) return &a;
    return nullptr;
}

void sm::animation_assets::validate() const {
    if (!find_pose(default_pose)) throw std::invalid_argument("Missing Default pose");

    std::unordered_set<object_id> ids;
    auto add = [&](object_id id) {
        if (id.is_nil() || !ids.insert(id).second)
            throw std::invalid_argument("Duplicate animation asset ID");
    };

    for (const auto& p : poses) {
        add(p.id);
        for (const auto& [id, pt] : p.node_positions)
            if (id.is_nil() || !std::isfinite(pt.x) || !std::isfinite(pt.y))
                throw std::invalid_argument("Invalid pose position");
    }
    for (const auto& a : animations) add(a.id);
}

void sm::animation_assets::validate(const topology& topology,
        std::span<const object_id> rig_skeletons) const {
    validate();
    const std::unordered_set<object_id> rig(rig_skeletons.begin(), rig_skeletons.end());
    for (const auto id : rig_skeletons)
        if (!topology.skeleton(id)) throw std::invalid_argument("Character rig contains a missing skeleton");

    for (const auto& p : poses) {
        for (const auto& [id, pt] : p.node_positions) {
            auto node = topology.get<sm::node>(id);
            if (!node || !rig.contains(node->get().owner().id()))
                throw std::invalid_argument("Pose contains a node outside the character rig");
        }
    }
}

sm::pose sm::capture_pose(const topology& topology, const std::vector<object_id>& skeletons,
        std::string name) {
    pose p;
    p.name = std::move(name);
    for (const auto& id : skeletons) if (auto s = topology.skeleton(id))
        for (auto n : s->get().nodes()) p.node_positions.emplace(n->id(), n->world_pos());
    return p;
}

void sm::initialize_animation_assets(animation_assets& assets, const topology& topology,
        const std::vector<object_id>& skeletons) {
    if (!assets.poses.empty() || skeletons.empty()) return;
    auto p = capture_pose(topology, skeletons, "Default");
    assets.default_pose = p.id;
    assets.poses.push_back(std::move(p));
}

void sm::reconcile_animation_poses(animation_assets& assets, const topology& topology,
        const std::vector<object_id>& skeletons) {
    initialize_animation_assets(assets, topology, skeletons);
    if (assets.poses.empty()) return;

    std::unordered_map<object_id, point> members;
    for (const auto& sid : skeletons) if (auto skel = topology.skeleton(sid))
        for (auto node : skel->get().nodes()) members.emplace(node->id(), node->world_pos());

    for (auto& p : assets.poses) {
        if (p.id == assets.default_pose) {
            std::erase_if(p.node_positions, [&](const auto& entry) { return !members.contains(entry.first); });
            for (const auto& [id, pt] : members) p.node_positions.try_emplace(id, pt);
        } else {
            std::erase_if(p.node_positions, [&](const auto& entry) { return !members.contains(entry.first); });
        }
    }
}

void sm::remap_animation_assets(animation_assets& assets,
        const std::unordered_map<object_id, object_id>& id_remap) {
    for (auto& p : assets.poses) {
        std::unordered_map<object_id, point> remapped;
        remapped.reserve(p.node_positions.size());
        for (const auto& [old_id, pt] : p.node_positions) {
            auto id = old_id;
            if (auto it = id_remap.find(id); it != id_remap.end()) id = it->second;
            if (!remapped.emplace(id, pt).second)
                throw std::invalid_argument("Pose remap produced duplicate node IDs");
        }
        p.node_positions = std::move(remapped);
    }
}

void sm::apply_pose(const pose& pose, const topology& topology) {
    geometry_batch batch(topology);
    for (const auto& [id, pt] : pose.node_positions)
        if (auto node = topology.get<sm::node>(id)) node->get().set_world_pos(pt);
    if (batch.commit() != result::success)
        throw std::invalid_argument("pose violates rigid constraints");
}

bool sm::pose_compatible(const pose& pose, const topology& topology,
        const std::vector<object_id>& skeletons) {
    std::size_t count = 0;
    for (const auto& id : skeletons) if (auto s = topology.skeleton(id)) for (auto n : s->get().nodes()) {
        ++count;
        if (!pose.node_positions.contains(n->id())) return false;
    }
    return count == pose.node_positions.size();
}
