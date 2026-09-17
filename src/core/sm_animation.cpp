#include "sm_animation.hpp"
#include "sm_skeleton.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <unordered_set>

double sm::ease(easing curve, double u) {
    u = std::clamp(u, 0.0, 1.0);
    switch (curve) {
    case easing::ease_in: return u * u;
    case easing::ease_out: return u * (2 - u);
    case easing::ease_in_out: return u < .5 ? 2*u*u : 1 - 2*(1-u)*(1-u);
    case easing::smoothstep: return u*u*(3-2*u);
    default: return u;
    }
}
sm::animation_time sm::animation::duration() const {
    animation_time end = 0;
    for (const auto& layer : layers) for (const auto& a : layer.actions) {
        if (a.start < 0 || a.duration <= 0 || a.start > std::numeric_limits<animation_time>::max() - a.duration)
            throw std::invalid_argument("Invalid action interval");
        end = std::max(end, a.start + a.duration);
    }
    return end;
}
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
    auto add = [&](object_id id) { if (id.is_nil() || !ids.insert(id).second) throw std::invalid_argument("Duplicate animation asset ID"); };
    for (const auto& p : poses) {
        add(p.id);
        for (const auto& [id, pt] : p.node_positions)
            if (id.is_nil() || !std::isfinite(pt.x) || !std::isfinite(pt.y)) throw std::invalid_argument("Invalid pose position");
    }
    for (const auto& a : animations) {
        add(a.id);
        if (!find_pose(a.base_pose)) throw std::invalid_argument("Missing animation base pose");
        a.duration();
        for (const auto& layer : a.layers) {
            std::vector<const animation_action*> sorted;
            for (const auto& action : layer.actions) { add(action.id); sorted.push_back(&action); }
            std::ranges::sort(sorted, {}, [](auto a) { return a->start; });
            for (std::size_t i = 1; i < sorted.size(); ++i)
                if (sorted[i-1]->start + sorted[i-1]->duration > sorted[i]->start)
                    throw std::invalid_argument("Actions overlap within a layer");
        }
    }
}
sm::pose sm::capture_pose(const topology& topology, const std::vector<object_id>& skeletons, std::string name) {
    pose p; p.name = std::move(name);
    for (const auto& id : skeletons) if (auto s = topology.skeleton(id))
        for (auto n : s->get().nodes()) p.node_positions.emplace(n->id(), n->world_pos());
    return p;
}
void sm::initialize_animation_assets(animation_assets& assets, const topology& topology, const std::vector<object_id>& skeletons) {
    if (!assets.poses.empty() || skeletons.empty()) return;
    auto p = capture_pose(topology, skeletons, "Default");
    assets.default_pose = p.id;
    assets.poses.push_back(std::move(p));
    assets.character_root = topology.skeleton(skeletons.front())->get().root_node().id();
}
void sm::apply_pose(const pose& pose, const topology& topology) {
    for (const auto& [id, pt] : pose.node_positions) if (auto node = topology.get<sm::node>(id)) node->get().set_world_pos(pt);
}
bool sm::pose_compatible(const pose& pose, const topology& topology, const std::vector<object_id>& skeletons) {
    std::size_t count = 0;
    for (const auto& id : skeletons) if (auto s = topology.skeleton(id)) for (auto n : s->get().nodes()) {
        ++count;
        if (!pose.node_positions.contains(n->id())) return false;
    }
    return count == pose.node_positions.size();
}
