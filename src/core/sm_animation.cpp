#include "sm_animation.h"
#include "sm_skeleton.h"
#include <algorithm>

namespace  {

} 

void sm::pose::set_node_position(const std::string& node_name, const sm::point& pos) {
    node_positions_[node_name] = pos;
}

std::optional<sm::point> sm::pose::node_position(const std::string& node_name) const {
    auto it = node_positions_.find(node_name);
    if (it != node_positions_.end()) {
        return it->second;
    }
    return std::nullopt;
}

void sm::pose::apply(sm::skeleton& skel) const {
    // TODO: Apply the pose to the skeleton nodes.
}

sm::animation_script::animation_script(const std::string& name) : name_(name) {}

void sm::animation_script::insert(int start_time_ms, const sm::animation_event& event) {
    timeline_[start_time_ms].push_back(event);
}

void sm::animation_script::set(int start_time_ms, const sm::animation_event& event, int index) {
    if (timeline_.contains(start_time_ms) && index < static_cast<int>(timeline_.at(start_time_ms).size())) {
        timeline_.at(start_time_ms)[index] = event;
    }
}

std::vector<sm::animation_event> sm::animation_script::events(int start_time_ms) const {
    if (timeline_.contains(start_time_ms)) {
        return timeline_.at(start_time_ms);
    }
    return {};
}

const std::string& sm::animation_script::name() const {
    return name_;
}

sm::animation sm::animation_script::bake(const sm::skeleton& skel) const {
    // TODO: Bake the script into a compiled animation.
    return sm::animation(name_);
}

sm::animation::animation(const std::string& name) : name_(name) {}

sm::pose sm::animation::pose_at_time(int time_ms) const {
    // TODO: Return the pose at the given time (stub for now)
    return sm::pose();
}

const std::string& sm::animation::name() const {
    return name_;
}

void sm::animation::add_keyframe_internal(int time_ms, const sm::pose& p) {
    keyframes_.emplace_back(time_ms, p);
}

