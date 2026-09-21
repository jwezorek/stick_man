#include "sm_animation.hpp"
#include "sm_skeleton.hpp"
#include "sm_visit.hpp"
#include <algorithm>
#include <stdexcept>
#include <type_traits>
#include <unordered_set>

namespace {
using scope = std::unordered_set<sm::object_id>;
scope ik_scope(sm::object_id effector, const std::vector<sm::object_id>& pins, const sm::topology& topology) {
    scope result;
    const scope boundaries(pins.begin(), pins.end());
    if (auto node = topology.get<sm::node>(effector)) {
        sm::visit_nodes_and_bones(node->get(), [&](const sm::node& n) {
            if (boundaries.contains(n.id())) return sm::visit_result::terminate_branch;
            result.insert(n.id());
            return sm::visit_result::continue_traversal;
        });
    }
    return result;
}
std::optional<sm::object_id> reference_bone(const sm::animation_action& action, sm::object_id root) {
    return std::visit([&](const auto& data) -> std::optional<sm::object_id> {
        using T = std::decay_t<decltype(data)>;
        if constexpr (std::is_same_v<T, sm::rigid_translation> || std::is_same_v<T, sm::ik_translation>) {
            if (data.reference == sm::translation_reference::character_root) return root;
            if (data.reference == sm::translation_reference::bone) return data.reference_bone;
        }
        return {};
    }, action.data);
}
bool overlaps(const sm::animation_layer& layer, const sm::animation_action& action) {
    return std::ranges::any_of(layer.actions, [&](const auto& other) {
        return action.start < other.start + other.duration && other.start < action.start + action.duration;
    });
}
}

std::vector<sm::object_id> sm::animation_action_write_scope(const animation_action& action, const topology& topology) {
    scope result;
    std::visit([&](const auto& data) {
        using T = std::decay_t<decltype(data)>;
        if constexpr (std::is_same_v<T, rigid_translation>) {
            for (auto id : data.skeletons) if (auto skeleton = topology.skeleton(id))
                for (auto node : skeleton->get().nodes()) result.insert(node->id());
        } else if constexpr (std::is_same_v<T, rigid_rotation>) {
            if (auto bone = topology.get<sm::bone>(data.bone)) {
                const auto pivot = data.pivot == rotation_pivot::root
                    ? bone->get().parent_node().id() : bone->get().child_node().id();
                // rotate_by reconstructs the entire hierarchy and reapplies constraints,
                // including in bone_only mode. Only the pivot is guaranteed fixed.
                for (auto node : bone->get().owner().nodes()) if (node->id() != pivot) result.insert(node->id());
            }
        } else if constexpr (std::is_same_v<T, ik_translation>) {
            result = ik_scope(data.effector, data.pins, topology);
        } else if constexpr (std::is_same_v<T, ik_rotation>) {
            result = ik_scope(data.effector, {data.pivot_node}, topology);
        }
    }, action.data);
    std::vector<object_id> ordered(result.begin(), result.end());
    std::ranges::sort(ordered);
    return ordered;
}

void sm::validate_animation_order(const animation& animation, object_id root, const topology& topology) {
    animation.duration();
    // Forward scan: once an action has captured a frame, later actions must
    // not write either endpoint. Check before registering to exempt self writes.
    scope captured;
    for (const auto& layer : animation.layers) {
        std::vector<const animation_action*> actions;
        for (const auto& action : layer.actions) actions.push_back(&action);
        std::ranges::stable_sort(actions, {}, [](auto a) { return a->start; });
        for (const auto* action : actions) {
            for (auto node : animation_action_write_scope(*action, topology))
                if (captured.contains(node)) throw std::invalid_argument(
                    "An action moves a reference frame used below it. Place the reference-relative action above it.");
            if (auto id = reference_bone(*action, root)) if (auto bone = topology.get<sm::bone>(*id)) {
                captured.insert(bone->get().parent_node().id());
                captured.insert(bone->get().child_node().id());
            }
        }
    }
}

sm::animation sm::place_animation_action(const animation& requested, object_id id,
        object_id root, const topology& topology) {
    requested.duration();
    auto valid = [&](const animation& candidate) {
        try { validate_animation_order(candidate, root, topology); return true; }
        catch (const std::invalid_argument&) { return false; }
    };
    animation remaining = requested;
    std::optional<animation_action> action;
    std::size_t layer_index = 0;
    for (std::size_t i = 0; i < remaining.layers.size(); ++i) {
        for (const auto& candidate : remaining.layers[i].actions) if (candidate.id == id) {
            if (action) throw std::invalid_argument("Duplicate action ID");
            action = candidate; layer_index = i;
        }
        std::erase_if(remaining.layers[i].actions, [&](const auto& a) { return a.id == id; });
    }
    if (!action) throw std::invalid_argument("Action to place is missing");
    if (!overlaps(remaining.layers[layer_index], *action) && valid(requested)) return requested;

    // Search upward without changing the relative order of any existing action.
    // Prefer sharing the next layer when that gives the same valid placement;
    // otherwise insert a new layer at the first valid boundary.
    for (std::size_t boundary = layer_index + 1; boundary <= remaining.layers.size(); ++boundary) {
        auto between = remaining;
        between.layers.insert(between.layers.begin() + boundary, animation_layer{{*action}});
        if (valid(between)) {
            if (boundary < remaining.layers.size() && !overlaps(remaining.layers[boundary], *action)) {
                auto shared = remaining;
                shared.layers[boundary].actions.push_back(*action);
                if (valid(shared)) return shared;
            }
            return between;
        }
        if (boundary < remaining.layers.size() && !overlaps(remaining.layers[boundary], *action)) {
            auto shared = remaining;
            shared.layers[boundary].actions.push_back(*action);
            if (valid(shared)) return shared;
        }
    }
    throw std::invalid_argument(
        "These actions have conflicting animated reference-frame dependencies, and changing their layer order cannot resolve the conflict.\n\n"
        "Use Animation Root for a fixed reference frame, or change the reference bone or pins so the actions no longer affect each other's reference frames.");
}
