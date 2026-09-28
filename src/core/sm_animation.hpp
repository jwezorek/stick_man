#pragma once
#include "sm_types.hpp"
#include "sm_object_id.hpp"
#include "json_fwd.hpp"
#include <cstddef>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>
#include <span>

namespace sm {
    // Standalone named poses are independent project assets. They intentionally
    // retain their original world-node representation and are not animation keys.
    struct pose {
        object_id id = object_id::generate();
        std::string name;
        std::unordered_map<object_id, point> node_positions;
    };

    // Animation V2 keyframes store skeletal state in rig-local terms. Root node
    // translations are relative to the animation's incoming frame; bone values
    // are local rotations (root bones use the incoming frame as their parent).
    struct skeletal_pose {
        std::unordered_map<object_id, point> root_positions;
        std::unordered_map<object_id, double> bone_rotations;
    };

    struct pose_keyframe {
        object_id id = object_id::generate();
        std::optional<std::string> name;
        skeletal_pose pose;
    };

    struct pose_transition {
        object_id id = object_id::generate();
        double duration_seconds = 0.4;
    };

    struct animation {
        object_id id = object_id::generate();
        std::string name;
        std::vector<pose_keyframe> keyframes;
        // Invariant: transitions.size() == max(keyframes.size() - 1, 0).
        std::vector<pose_transition> transitions;

        const pose_keyframe* find_keyframe(object_id id) const;
        pose_keyframe* find_keyframe(object_id id);
        std::optional<std::size_t> keyframe_index(object_id id) const;
        void reconcile_transitions();
        double duration_seconds() const;
    };

    struct animation_assets {
        object_id default_pose;
        std::vector<pose> poses;
        std::vector<animation> animations;

        const pose* find_pose(object_id id) const;
        const animation* find_animation(object_id id) const;
        animation* find_animation(object_id id);
        void validate() const;
        void validate(const topology& topology, std::span<const object_id> rig_skeletons) const;
    };

    pose capture_pose(const topology& topology, const std::vector<object_id>& skeletons, std::string name);
    skeletal_pose capture_skeletal_pose(const topology& topology, std::span<const object_id> skeletons);
    void apply_skeletal_pose(const skeletal_pose& pose, const topology& topology,
        std::span<const object_id> skeletons);
    bool skeletal_pose_compatible(const skeletal_pose& pose, const topology& topology,
        std::span<const object_id> skeletons);
    void initialize_animation_assets(animation_assets& assets, const topology& topology,
        const std::vector<object_id>& skeletons);
    void reconcile_animation_poses(animation_assets& assets, const topology& topology,
        const std::vector<object_id>& skeletons);
    void remap_animation_assets(animation_assets& assets,
        const std::unordered_map<object_id, object_id>& id_remap);
    void apply_pose(const pose& pose, const topology& topology);
    bool pose_compatible(const pose& pose, const topology& topology,
        const std::vector<object_id>& skeletons);

    nlohmann::json animation_assets_to_json(const animation_assets& assets);
    animation_assets animation_assets_from_json(const nlohmann::json& json);
}
