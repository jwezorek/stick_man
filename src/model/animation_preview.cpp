#include "project.hpp"
#include "../core/sm_constraint_geometry.hpp"

const sm::topology& mdl::project::display_topology() const {
    return playback_topology_ ? *playback_topology_ : topology();
}

void mdl::project::exit_animation_preview() {
    // Stop transport before another preview tick can run.
    emit animation_editing_requested();
    if (playback_topology_) {
        emit animation_display_changing(false);
        playback_topology_.reset();
        playback_pinned_node_ids_.clear();
        playback_rotation_constraints_.clear();
        playback_transition_id_.reset();
        emit animation_display_changed();
    }
    playback_status_ = animation_display_status::editing;
    playback_error_.reset();
    emit animation_display_status_changed();
}

mdl::animation_display_status mdl::project::preview_animation_time(double seconds) {
    if (!animation_session_)
        return playback_status_;
    const auto fail = [&](animation_display_status status, std::optional<sm::result> error) {
        exit_animation_preview();
        playback_status_ = status;
        playback_error_ = error;
        emit animation_display_status_changed();
        return status;
    };
    const auto character = core_.character(animation_session_->character);
    const auto* animation = character ? character->get().animation_data().find_animation(
        animation_session_->animation) : nullptr;
    if (!animation)
        return fail(animation_display_status::sampling_failed, sm::result::not_found);
    const auto skeletons = character->get().rig().skeleton_ids();
    // Sample against persistent rig geometry, not the current display topology.
    auto sample = sm::sample_constrained_pose(*animation, seconds, core_.topology(), skeletons,
        character->get().character_root_bone());
    if (!sample)
        return fail(animation_display_status::sampling_failed, sample.error());
    if (!*sample)
        return fail(animation_display_status::empty, {});

    std::unique_ptr<sm::topology> candidate;
    try {
        candidate = std::make_unique<sm::topology>();
        for (auto id : skeletons) {
            auto copy = core_.topology().skeleton(id)->get().copy_to(*candidate);
            if (!copy)
                return fail(animation_display_status::reconstruction_failed, copy.error());
            copy->get().clear_user_data();
            for (auto n : copy->get().nodes()) n->clear_user_data();
            for (auto b : copy->get().bones()) b->clear_user_data();
        }
        auto display_pose = (**sample).pose;
        // Normalize only the rendering copy; keep authored key values untouched.
        for (auto& [id, angle] : display_pose.bone_rotations)
            angle = sm::normalize_angle(angle);
        sm::apply_skeletal_pose(display_pose, *candidate, skeletons);
        const auto valid = sm::constraint_geometry(*candidate).validate();
        if (valid != sm::result::success)
            return fail(animation_display_status::reconstruction_failed, valid);
    } catch (const std::exception&) {
        return fail(animation_display_status::reconstruction_failed, sm::result::out_of_bounds);
    }
    // Remember the transition associated with the sampled frame, including
    // the outgoing transition at an exact (non-terminal) keyframe.  Path
    // adornments use the same transition selection as pins and rotations.
    std::optional<sm::object_id> sampled_transition;
    if (const auto* tr = std::get_if<sm::reference_transition>(&(**sample).location)) {
        sampled_transition = tr->transition_id;
    } else if (const auto* key = std::get_if<sm::reference_keyframe>(&(**sample).location)) {
        if (const auto index = animation->keyframe_index(key->keyframe_id);
                index && *index < animation->transitions.size())
            sampled_transition = animation->transitions[*index].id;
    }
    // Publish the sample only after reconstruction succeeds.
    emit animation_display_changing(true);
    playback_topology_ = std::move(candidate);
    playback_pinned_node_ids_ = (**sample).pinned_nodes;
    playback_rotation_constraints_ = (**sample).rotation_constraints;
    playback_transition_id_ = sampled_transition;
    playback_status_ = animation_display_status::sampled;
    playback_error_.reset();
    emit animation_display_changed();
    emit animation_display_status_changed();
    return playback_status_;
}
