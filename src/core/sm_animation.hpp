#pragma once
#include "sm_types.hpp"
#include "sm_object_id.hpp"
#include "json_fwd.hpp"
#include <string>
#include <unordered_map>
#include <vector>
#include <span>

namespace sm {
    // Animation V2 is deliberately empty in Phase 1.  Skeletal frame/keyframe
    // data belongs to later phases; the persistent asset currently has identity
    // and a user-visible name only.
    struct animation {
        object_id id = object_id::generate();
        std::string name;
    };

    // Standalone named poses are independent project assets.  They are not
    // animation keyframes and remain useful outside Animation Mode.
    struct pose {
        object_id id = object_id::generate();
        std::string name;
        std::unordered_map<object_id, point> node_positions;
    };

    struct animation_assets {
        object_id default_pose;
        std::vector<pose> poses;
        std::vector<animation> animations;

        const pose* find_pose(object_id id) const;
        const animation* find_animation(object_id id) const;
        void validate() const;
        void validate(const topology& topology, std::span<const object_id> rig_skeletons) const;
    };

    pose capture_pose(const topology& topology, const std::vector<object_id>& skeletons, std::string name);
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
