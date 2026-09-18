#pragma once
#include "sm_types.hpp"
#include "sm_object_id.hpp"
#include "json_fwd.hpp"
#include <cstdint>
#include <string>
#include <variant>
#include <vector>
#include <unordered_map>

namespace sm {
    // Authored times are integer milliseconds; layers are ordered bottom to top.
    using animation_time = std::int64_t;
    enum class easing { linear, ease_in, ease_out, ease_in_out, smoothstep };
    double ease(easing curve, double progress);
    enum class rotation_pivot { root, tip };
    enum class rotation_propagation { hierarchy, bone_only };
    struct rigid_rotation {
        object_id bone;
        rotation_pivot pivot = rotation_pivot::root;
        double angle = 0;
        rotation_propagation propagation = rotation_propagation::hierarchy;
    };
    struct ik_rotation {
        object_id effector;
        object_id pivot_node;
        double angle = 0;
    };
    struct rigid_translation { std::vector<object_id> skeletons; point offset{}; };
    enum class target_reference { character_start, character_root, node };
    struct line_path { point start{}, end{}; };
    struct cubic_bezier_path { point start{}, control1{}, control2{}, end{}; };
    struct spline_path { std::vector<cubic_bezier_path> segments; };
    using target_path = std::variant<line_path, cubic_bezier_path, spline_path>;
    struct ik_translation {
        object_id effector;
        std::vector<object_id> pins;
        target_reference reference = target_reference::character_start;
        object_id reference_node;
        target_path path = line_path{};
    };
    using action_data = std::variant<rigid_rotation, ik_rotation, rigid_translation, ik_translation>;
    struct animation_action {
        object_id id = object_id::generate();
        animation_time start = 0;
        animation_time duration = 1000;
        sm::easing easing = easing::linear;
        action_data data = rigid_rotation{};
    };
    struct animation_layer { std::vector<animation_action> actions; };
    struct animation {
        object_id id = object_id::generate();
        std::string name;
        object_id base_pose;
        std::vector<animation_layer> layers;
        animation_time duration() const;
    };
    struct pose {
        object_id id = object_id::generate();
        std::string name;
        std::unordered_map<object_id, point> node_positions;
    };
    struct animation_assets {
        object_id character_root;
        object_id default_pose;
        std::vector<pose> poses;
        std::vector<animation> animations;
        const pose* find_pose(object_id id) const;
        const animation* find_animation(object_id id) const;
        void validate() const;
    };
    pose capture_pose(const topology& topology, const std::vector<object_id>& skeletons, std::string name);
    void initialize_animation_assets(animation_assets& assets, const topology& topology, const std::vector<object_id>& skeletons);
    void apply_pose(const pose& pose, const topology& topology);
    bool pose_compatible(const pose& pose, const topology& topology, const std::vector<object_id>& skeletons);
    struct animation_evaluation {
        std::vector<object_id> invalid_actions;
        std::vector<object_id> unsupported_actions;
    };
    // Evaluation always resets detached working geometry to the base pose; it never
    // integrates from the previously displayed frame.
    animation_evaluation evaluate_animation(const animation& animation, const pose& base,
        topology& working, animation_time time);
    nlohmann::json animation_assets_to_json(const animation_assets& assets);
    animation_assets animation_assets_from_json(const nlohmann::json& json);
}
