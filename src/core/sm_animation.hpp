#pragma once
#include "sm_types.hpp"
#include "sm_object_id.hpp"
#include "json_fwd.hpp"
#include <cstdint>
#include <string>
#include <variant>
#include <vector>
#include <unordered_map>
#include <utility>
#include <optional>
#include <span>

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

    enum class motion_path_kind { straight, curve, spline };
    struct line_path { point start{}, end{}; };
    struct cubic_bezier_path { point start{}, control1{}, control2{}, end{}; };
    struct spline_path { std::vector<cubic_bezier_path> segments; };
    using motion_path_geometry = std::variant<line_path, cubic_bezier_path, spline_path>;

    // A motion path is a displacement path: its first point is always {0,0}.
    // Arc-length data is derived/cached and is intentionally not persisted.
    class motion_path {
        struct arc_sample {
            std::size_t segment = 0;
            double parameter = 0.0;
            double cumulative = 0.0;
        };
        motion_path_geometry geometry_ = line_path{};
        mutable std::vector<arc_sample> arc_cache_;
        mutable double total_length_ = 0.0;
        void rebuild_arc_cache() const;
    public:
        motion_path() = default;
        motion_path(line_path path) : geometry_(std::move(path)) {}
        motion_path(cubic_bezier_path path) : geometry_(std::move(path)) {}
        motion_path(spline_path path) : geometry_(std::move(path)) {}
        explicit motion_path(motion_path_geometry path) : geometry_(std::move(path)) {}

        const motion_path_geometry& geometry() const noexcept { return geometry_; }
        void set_geometry(motion_path_geometry path);
        motion_path_kind kind() const noexcept;
        point final_displacement() const;
        double length() const;
        point evaluate_by_arc_length(double distance) const;
    };

    enum class translation_reference { animation_root, character_root, bone };

    // A renderer-independent 2D reference frame used by translation actions.
    // Motion paths are stored in this local coordinate system and converted to
    // world space only when they are evaluated or presented by a client.
    struct reference_frame {
        point origin{};
        double angle = 0.0;

        point vector_to_world(point local) const;
        point vector_to_local(point world) const;
        point local_to_world(point local) const;
        point world_to_local(point world) const;
    };

    struct rigid_translation {
        std::vector<object_id> skeletons;
        motion_path path;
        translation_reference reference = translation_reference::animation_root;
        object_id reference_bone;
    };
    struct ik_translation {
        object_id effector;
        std::vector<object_id> pins;
        // The path is a displacement from the incoming evaluated effector position,
        // expressed in the selected reference frame.
        motion_path path;
        translation_reference reference = translation_reference::animation_root;
        object_id reference_bone;
    };
    using action_data = std::variant<rigid_rotation, ik_rotation, rigid_translation, ik_translation>;
    enum class animation_dependency_kind { node, bone, skeleton };
    struct animation_dependency {
        animation_dependency_kind kind;
        object_id id;
        bool operator==(const animation_dependency&) const = default;
    };
    struct animation_action {
        object_id id = object_id::generate();
        animation_time start = 0;
        animation_time duration = 1000;
        sm::easing easing = easing::linear;
        action_data data = rigid_rotation{};
    };
    // The single authoritative description of persistent project-object references
    // carried by an action. This visitor is intentionally exhaustive: adding a new
    // action_data alternative must also define its dependencies here.
    std::vector<animation_dependency> animation_action_dependencies(const animation_action& action);
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
        object_id default_pose;
        std::vector<pose> poses;
        std::vector<animation> animations;
        const pose* find_pose(object_id id) const;
        const animation* find_animation(object_id id) const;
        void validate() const;
        // Validate assets in the context of the owning character. Persistent topology
        // references are legal only when they resolve inside that character's rig.
        void validate(const topology& topology, std::span<const object_id> rig_skeletons,
            object_id character_root_bone) const;
    };
    pose capture_pose(const topology& topology, const std::vector<object_id>& skeletons, std::string name);
    void initialize_animation_assets(animation_assets& assets, const topology& topology, const std::vector<object_id>& skeletons);
    // Keep pose data coherent as a character's rig evolves. The Default pose
    // tracks character membership automatically while named poses retain their
    // authored positions, losing only entries for nodes that no longer exist.
    void reconcile_animation_poses(animation_assets& assets, const topology& topology,
        const std::vector<object_id>& skeletons);
    // Remap every persistent topology reference stored by poses and actions.
    // Asset identities (pose/animation/action IDs) are intentionally unchanged.
    void remap_animation_assets(animation_assets& assets,
        const std::unordered_map<object_id, object_id>& id_remap);
    void apply_pose(const pose& pose, const topology& topology);
    bool pose_compatible(const pose& pose, const topology& topology, const std::vector<object_id>& skeletons);
    struct rotation_evaluation_context {
        point pivot{};
        point rotating{};
    };
    struct action_evaluation_context {
        std::optional<reference_frame> translation_reference_frame;
        // For IK translations this is the effector position immediately before
        // the action contributes, in the same deterministic working topology.
        std::optional<point> translation_anchor_world;
        std::optional<rotation_evaluation_context> rotation;
    };
    struct animation_evaluation {
        std::vector<object_id> invalid_actions;
        std::vector<object_id> unsupported_actions;
        std::vector<object_id> evaluation_order;
        std::unordered_map<object_id, action_evaluation_context> contexts;
    };

    // Resolve translation reference spaces without introducing any UI types.
    // Animation Root is always taken from the base pose; Character Root and Bone
    // are taken from the currently evaluated topology.
    std::optional<reference_frame> bone_reference_frame(object_id bone_id, const topology& topology);
    std::optional<reference_frame> translation_reference_frame(translation_reference reference,
        object_id reference_bone, object_id character_root_bone, const pose& base, const topology& working);

    // Visible composition order: bottom-to-top layers, chronological within each layer.
    std::vector<object_id> animation_evaluation_order(const animation& animation);
    std::vector<object_id> animation_action_write_scope(const animation_action& action, const topology& topology);
    void validate_animation_order(const animation& animation, object_id character_root_bone, const topology& topology);
    // The action already occupies its requested position. Return a valid copy,
    // moving only this action upward if necessary; throw when no placement exists.
    animation place_animation_action(const animation& requested, object_id action,
        object_id character_root_bone, const topology& topology);

    // Evaluation always resets detached working geometry to the base pose; it never
    // integrates from the previously displayed frame.
    animation_evaluation evaluate_animation(const animation& animation, const pose& base,
        object_id character_root_bone, topology& working, animation_time time);
    nlohmann::json animation_assets_to_json(const animation_assets& assets);
    animation_assets animation_assets_from_json(const nlohmann::json& json);
}
