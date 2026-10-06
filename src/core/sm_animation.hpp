#pragma once
#include "sm_types.hpp"
#include "sm_object_id.hpp"
#include "json_fwd.hpp"
#include <cstddef>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <span>
#include <variant>

namespace sm {
    // Standalone named poses are independent project assets. They intentionally
    // retain their original world-node representation and are not animation keys.
    struct pose {
        object_id id = object_id::generate();
        std::string name;
        std::unordered_map<object_id, point> node_positions;
    };

    // Current full-rig capture stores root-node world positions and local bone
    // rotations in radians. Incoming-frame/domain conversion is not implemented.
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

    // Stateless, Qt-independent reference sampling; never applies or solves a pose.
    // An interior reference may violate persistent constraints.
    // Empty sequences return nullopt. Finite times clamp to the authored range;
    // a single keyframe is returned for every finite time. Cumulative double
    // timestamps use exact equality (no epsilon); stored endpoint scalars are copied.
    // If floating-point addition collapses timestamps, the last keyframe at that
    // timestamp wins, matching Pose Strip timing.
    // Interior roots use component-wise linear interpolation. Angles normalize
    // endpoints, use angular_distance's shortest signed arc, then normalize the
    // result. Half-turn ties follow the sign of the normalized endpoint difference
    // (+pi or -pi). Equivalent orientations encode no full turn; progress is linear.
    // Throws std::invalid_argument for nonfinite time, invalid local asset IDs,
    // malformed timing, nonfinite pose values or differing root/bone ID sets
    // anywhere in the sequence, even when sampling an exact/clamped endpoint.
    // Project/load validation remains responsible for topology and rig integrity.
    std::optional<reference_pose_sample> sample_reference_pose(
        const animation& animation, double time_seconds);

    // Unlike a reference_pose_sample, this is returned only after feasibility
    // validation. The location distinguishes exact stored keys from interiors.
    struct constrained_pose_sample {
        skeletal_pose pose;
        std::variant<reference_keyframe, reference_transition> location;
        // Interior samples report the active transition's pins. Exact non-terminal
        // keys report their outgoing transition's pins; the final key reports none.
        std::unordered_set<object_id> pinned_nodes;
    };
    using constrained_pose_result = std::expected<std::optional<constrained_pose_sample>, result>;

    // Full-rig, stateless persistent-constraint sampling; no model writes.
    // Transition pins are positional constraints on interior samples: the active
    // transition's pin set is held at its source-pose position. Exact keys are never
    // altered by transition-local pins.
    // The explicit skeleton IDs identify the entire rig in the supplied topology.
    // Lengths/scales are frozen from that topology's geometry at entry. Unpinned
    // roots remain exactly as sampled; a transition-pinned root remains at its
    // source-pose position. A constraint crossing the rig boundary (in either
    // direction) is unsupported: invalid_membership, never an expanded solve.
    // Timing/interpolation comes exclusively from sample_reference_pose. Empty is
    // a successful nullopt; malformed animation/time is invalid_animation. Missing,
    // duplicate or incompatible rig membership is invalid_membership; nonfinite or
    // degenerate rig geometry is out_of_bounds. Invalid constraint definitions keep
    // their Core status. Infeasible exact keys and proven empty angular intersections
    // are unsatisfiable_constraints. Exhausted/numerically failed projection is
    // ik_no_solution_found (not a proof of global infeasibility). No error has a pose.
    // Exact keys bypass optimization and retain every stored scalar. Feasible
    // references pass through. Otherwise bounded deterministic SLSQP minimizes the
    // equally weighted mean squared circular chord distance of LOCAL bone angles:
    // mean(2*(1-cos(local-reference))). No position objective or regularization.
    // This is a local projection, not a guaranteed global nearest pose. Candidates
    // are independently reconstructed/validated in detached transactional geometry:
    // 1e-8 radians for angular/fan feasibility and 1e-8 relative length tolerance.
    // Reconstructed lengths must be positive and finite. Unrepresentable FK
    // fails. No invalid reference/candidate is committed, including on exact keys.
    constrained_pose_result sample_constrained_pose(const animation& animation,
        double time_seconds, const topology& topology, std::span<const object_id> rig_skeletons);

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
