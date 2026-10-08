#pragma once
#include "sm_types.hpp"
#include "sm_object_id.hpp"
#include "sm_constraint.hpp"
#include "sm_animation_path.hpp"
#include "json_fwd.hpp"
#include <cstddef>
#include <optional>
#include <map>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <span>
#include <variant>

namespace sm {
    // Standalone poses remain world-node assets, separate from animation keys.
    struct pose {
        object_id id = object_id::generate();
        std::string name;
        std::unordered_map<object_id, point> node_positions;
    };

    // Full-rig poses store root world positions and local bone rotations in radians.
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
        std::unordered_set<object_id> pinned_nodes;
        constraint_map rotation_constraints;
        std::map<object_id, animation_path> paths; // key = constrained node ID
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

    struct reference_keyframe {
        object_id keyframe_id;
    };

    struct reference_transition {
        object_id transition_id;
        object_id from_keyframe_id;
        object_id to_keyframe_id;
        double progress;
    };

    struct reference_pose_sample {
        skeletal_pose pose;
        std::variant<reference_keyframe, reference_transition> location;
    };

    // Stateless reference sampling. Finite times clamp to the authored range;
    // empty animations return nullopt. Interior roots interpolate linearly and
    // local angles follow the shortest signed arc. Invalid animation data or time
    // throws std::invalid_argument; topology membership is validated elsewhere.
    std::optional<reference_pose_sample> sample_reference_pose(
        const animation& animation, double time_seconds);

    // Returned only after constraint feasibility has been established.
    struct constrained_pose_sample {
        skeletal_pose pose;
        std::variant<reference_keyframe, reference_transition> location;
        // Interior samples report the active transition's pins. Exact non-terminal
        // keys report their outgoing transition's pins; the final key reports none.
        std::unordered_set<object_id> pinned_nodes;
        constraint_map rotation_constraints;
    };
    using constrained_pose_result = std::expected<std::optional<constrained_pose_sample>, result>;

    // Stateless constrained sampling over the supplied rig. Transition pins hold
    // source-pose positions on interior samples; exact keys retain authored values.
    // Persistent and transition constraints are projected in detached geometry.
    // Errors distinguish invalid data/membership, infeasible constraints, and
    // numerical projection failure; no failed result contains a pose.
    constrained_pose_result sample_constrained_pose(const animation& animation,
        double time_seconds, const topology& topology, std::span<const object_id> rig_skeletons,
        std::optional<object_id> character_root_bone = std::nullopt);

    // Project a pose onto the supplied constraints and optional fixed node positions.
    std::expected<skeletal_pose, result> project_constrained_pose(
        const skeletal_pose& reference, const topology& topology,
        std::span<const object_id> rig_skeletons, const constraint_map& constraints,
        const std::map<object_id, point>& pinned_node_positions = {});

    // Resolve a node in an authored keyframe without changing caller geometry.
    std::optional<point> animation_pose_node(const skeletal_pose& pose,
        const topology& topology, std::span<const object_id> rig_skeletons, object_id node);
    std::optional<animation_root_frame> fixed_animation_root(const animation& animation,
        const topology& topology, std::span<const object_id> rig_skeletons,
        std::optional<object_id> character_root_bone);

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
