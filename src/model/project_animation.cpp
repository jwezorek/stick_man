#include "project.hpp"
#include "commands.hpp"
#include "../core/sm_geometry_batch.hpp"
#include "../core/sm_constraint_geometry.hpp"
#include <algorithm>
#include <cmath>
#include <iterator>
#include <map>
#include <stdexcept>
#include <unordered_set>
#include <utility>

/*------------------------------------------------------------------------------------------------*/
namespace {
    constexpr double k_animation_pin_position_tolerance = 1e-8;

    std::optional<sm::point> pose_node_position(const sm::skeletal_pose& pose,
            const sm::topology& reference, std::span<const sm::object_id> skeletons, sm::object_id node_id) {
        sm::topology copy;
        try {
            for (auto sid : skeletons) {
                auto skel = reference.skeleton(sid);
                if (!skel || !skel->get().copy_to(copy))
                    return {};
            }
            sm::apply_skeletal_pose(pose, copy, skeletons);
            auto node = copy.get<sm::node>(node_id);
            if (!node)
                return {};
            return node->get().world_pos();
        } catch (...) {
            return {};
        }
    }

    sm::result validate_pin_endpoints(const sm::animation& animation,
            const sm::topology& reference, std::span<const sm::object_id> skeletons) {
        for (std::size_t i = 0; i < animation.transitions.size(); ++i) {
            const auto& from = animation.keyframes[i];
            const auto& to = animation.keyframes[i + 1];
            for (auto node_id : animation.transitions[i].pinned_nodes) {
                auto a = pose_node_position(from.pose, reference, skeletons, node_id);
                auto b = pose_node_position(to.pose, reference, skeletons, node_id);
                if (!a || !b || sm::distance(*a, *b) > k_animation_pin_position_tolerance)
                    return sm::result::invalid_animation;
            }
        }
        return sm::result::success;
    }
    sm::constraint_map clone_transition_rotation_constraints(const sm::constraint_map& source) {
        sm::constraint_map result;
        for (const auto& [old_id, c] : source) {
            const auto* rotation = c.rotation();
            if (!rotation)
                continue;
            sm::object_id id;
            do id = sm::object_id::generate();
            while (result.contains(id));
            result.emplace(id, sm::constraint{id, c.name(), *rotation});
        }
        return result;
    }

    bool has_adjacent_transition_rotation_constraints(
            const sm::animation& animation, std::size_t keyframe_index) {
        return (keyframe_index > 0 &&
                    !animation.transitions[keyframe_index - 1].rotation_constraints.empty()) ||
            (keyframe_index < animation.transitions.size() &&
                    !animation.transitions[keyframe_index].rotation_constraints.empty());
    }

    std::expected<sm::constraint_map, sm::result> effective_keyframe_constraints(
            const sm::animation& animation, std::size_t keyframe_index,
            const sm::topology& reference) {
        auto constraints = reference.constraints();
        auto merge = [&](const sm::constraint_map& additions) -> sm::result {
            for (const auto& [id, constraint] : additions) {
                if (!constraints.emplace(id, constraint).second)
                    return sm::result::duplicate_id;
            }
            return sm::result::success;
        };
        if (keyframe_index > 0) {
            if (auto status = merge(animation.transitions[keyframe_index - 1].rotation_constraints);
                    status != sm::result::success)
                return std::unexpected(status);
        }
        if (keyframe_index < animation.transitions.size()) {
            if (auto status = merge(animation.transitions[keyframe_index].rotation_constraints);
                    status != sm::result::success)
                return std::unexpected(status);
        }
        if (auto status = sm::validate_constraints(reference, constraints);
                status != sm::result::success)
            return std::unexpected(status);
        return constraints;
    }

    std::expected<std::map<sm::object_id, sm::point>, sm::result> keyframe_pin_targets(
            const sm::animation& animation, std::size_t keyframe_index,
            const sm::topology& reference, std::span<const sm::object_id> skeletons) {
        std::map<sm::object_id, sm::point> targets;
        auto add = [&](sm::object_id node_id, const sm::skeletal_pose& pose) -> sm::result {
            auto position = pose_node_position(pose, reference, skeletons, node_id);
            if (!position)
                return sm::result::invalid_membership;
            auto [it, inserted] = targets.emplace(node_id, *position);
            if (!inserted && sm::distance(it->second, *position) > k_animation_pin_position_tolerance)
                return sm::result::invalid_animation;
            return sm::result::success;
        };
        if (keyframe_index > 0) {
            const auto& transition = animation.transitions[keyframe_index - 1];
            const auto& source = animation.keyframes[keyframe_index - 1].pose;
            for (auto node_id : transition.pinned_nodes) {
                if (auto status = add(node_id, source); status != sm::result::success)
                    return std::unexpected(status);
            }
        }
        if (keyframe_index < animation.transitions.size()) {
            const auto& transition = animation.transitions[keyframe_index];
            const auto& source = animation.keyframes[keyframe_index].pose;
            for (auto node_id : transition.pinned_nodes) {
                if (auto status = add(node_id, source); status != sm::result::success)
                    return std::unexpected(status);
            }
        }
        return targets;
    }

    std::expected<sm::skeletal_pose, sm::result> project_keyframe_to_transition_constraints(
            const sm::animation& candidate, std::size_t keyframe_index,
            const sm::animation& pin_target_source, const sm::topology& reference,
            std::span<const sm::object_id> skeletons) {
        if (!has_adjacent_transition_rotation_constraints(candidate, keyframe_index))
            return candidate.keyframes[keyframe_index].pose;
        auto constraints = effective_keyframe_constraints(candidate, keyframe_index, reference);
        if (!constraints)
            return std::unexpected(constraints.error());
        auto pins = keyframe_pin_targets(pin_target_source, keyframe_index, reference, skeletons);
        if (!pins)
            return std::unexpected(pins.error());
        return sm::project_constrained_pose(candidate.keyframes[keyframe_index].pose, reference,
            skeletons, *constraints, *pins);
    }

    sm::result validate_keyframe_transition_constraints(const sm::animation& animation,
            std::size_t keyframe_index, const sm::topology& reference,
            std::span<const sm::object_id> skeletons) {
        if (!has_adjacent_transition_rotation_constraints(animation, keyframe_index))
            return sm::result::success;
        sm::topology copy;
        for (auto sid : skeletons) {
            auto source = reference.skeleton(sid);
            if (!source)
                return sm::result::invalid_membership;
            auto copied = source->get().copy_to(copy);
            if (!copied)
                return copied.error();
        }
        try {
            sm::apply_skeletal_pose(animation.keyframes[keyframe_index].pose, copy, skeletons);
        } catch (const std::invalid_argument&) {
            return sm::result::invalid_animation;
        }
        auto constraints = effective_keyframe_constraints(animation, keyframe_index, copy);
        if (!constraints)
            return constraints.error();
        sm::constraint_geometry geometry(copy, *constraints);
        if (geometry.status() != sm::result::success)
            return geometry.status();
        return geometry.validate();
    }

    sm::result enforce_transition_rotation_endpoints(sm::animation& candidate,
            std::size_t transition_index, const sm::animation& pin_target_source,
            const sm::topology& reference, std::span<const sm::object_id> skeletons) {
        if (transition_index >= candidate.transitions.size())
            return sm::result::invalid_animation;
        for (auto keyframe_index : {transition_index, transition_index + 1}) {
            auto projected = project_keyframe_to_transition_constraints(candidate, keyframe_index,
                pin_target_source, reference, skeletons);
            if (!projected)
                return projected.error();
            candidate.keyframes[keyframe_index].pose = std::move(*projected);
        }
        return validate_pin_endpoints(candidate, reference, skeletons);
    }
}
/*------------------------------------------------------------------------------------------------*/
void mdl::project::transform(const std::vector<handle>& nodes,
        const std::function<void(sm::node&)>& fn) {
    if (animation_preview_active())
        return;
    if (!animation_session_ || !animation_session_->selected_keyframe) {
        auto cmd = commands::make_transform_bones_or_nodes_command(*this, nodes, {}, fn, {});
        if (animation_session_)
            execute_session_command(cmd);
        else
            execute_command(cmd);
        return;
    }

    // Probe the edit in session geometry, then commit it as one pose change.
    node_locs before, after;
    for (auto skel : topology().skeletons())
        for (auto node : skel->nodes())
            before.emplace_back(node->id(), node->world_pos());
    {
        sm::geometry_batch batch(topology());
        for (auto h : nodes)
            fn(commands::resolve<sm::node>(*this, h));
        if (batch.commit() != sm::result::success)
            return;
    }
    for (auto skel : topology().skeletons())
        for (auto node : skel->nodes())
            after.emplace_back(node->id(), node->world_pos());
    {
        sm::geometry_batch batch(topology());
        for (const auto& [h, pt] : before)
            commands::resolve<sm::node>(*this, h).set_world_pos(pt);
        if (batch.commit() != sm::result::success)
            throw std::runtime_error("unable to restore animation edit probe");
    }
    transform_node_positions(before, after);
}
void mdl::project::transform(const std::vector<handle>& bones,
        const std::function<void(sm::bone&)>& fn) {
    if (animation_preview_active())
        return;
    if (!animation_session_ || !animation_session_->selected_keyframe) {
        auto cmd = commands::make_transform_bones_or_nodes_command(*this, {}, bones, {}, fn);
        if (animation_session_)
            execute_session_command(cmd);
        else
            execute_command(cmd);
        return;
    }
    node_locs before, after;
    for (auto skel : topology().skeletons())
        for (auto node : skel->nodes())
            before.emplace_back(node->id(), node->world_pos());
    {
        sm::geometry_batch batch(topology());
        for (auto h : bones)
            fn(commands::resolve<sm::bone>(*this, h));
        if (batch.commit() != sm::result::success)
            return;
    }
    for (auto skel : topology().skeletons())
        for (auto node : skel->nodes())
            after.emplace_back(node->id(), node->world_pos());
    {
        sm::geometry_batch batch(topology());
        for (const auto& [h, pt] : before)
            commands::resolve<sm::node>(*this, h).set_world_pos(pt);
        if (batch.commit() != sm::result::success)
            throw std::runtime_error("unable to restore animation edit probe");
    }
    transform_node_positions(before, after);
}
void mdl::project::transform_node_positions(
        const node_locs& old_locs, const node_locs& new_locs) {
    if (animation_preview_active())
        return;
    if (!animation_session_ || !animation_session_->selected_keyframe) {
        auto cmd = commands::make_transform_node_positions_command(*this, old_locs, new_locs);
        if (animation_session_)
            execute_session_command(cmd);
        else
            execute_command(cmd);
        return;
    }

    const auto character_id = animation_session_->character;
    const auto animation_id = animation_session_->animation;
    const auto keyframe_id = *animation_session_->selected_keyframe;
    const auto skeletons = core_.character(character_id)->get().rig().skeleton_ids();
    auto* animation = core_.animation_data(character_id).find_animation(animation_id);
    auto* keyframe = animation ? animation->find_keyframe(keyframe_id) : nullptr;
    if (!keyframe)
        throw std::runtime_error("selected animation keyframe is missing");
    const auto before_pose = keyframe->pose;

    struct pose_edit_state {
        std::optional<sm::skeletal_pose> after_pose;
    };
    auto state = std::make_shared<pose_edit_state>();
    command cmd;
    cmd.redo = [character_id, animation_id, keyframe_id, new_locs,
            skeletons, before_pose, state](project& proj) {
        auto* animation = proj.core_.animation_data(character_id).find_animation(animation_id);
        auto* keyframe = animation ? animation->find_keyframe(keyframe_id) : nullptr;
        if (!keyframe)
            throw std::runtime_error("animation keyframe missing during pose edit");
        if (!state->after_pose) {
            sm::geometry_batch batch(proj.topology());
            for (const auto& [node_hnd, loc] : new_locs)
                commands::resolve<sm::node>(proj, node_hnd).set_world_pos(loc);
            if (batch.commit() != sm::result::success) {
                sm::apply_skeletal_pose(before_pose, proj.topology(), skeletons);
                emit proj.animation_authoring_error(QStringLiteral("This edit would violate the current animation pose constraints."));
                emit proj.refresh_canvas(proj, false);
                return sm::result::invalid_animation;
            }
            auto candidate_pose = sm::capture_skeletal_pose(proj.topology(), skeletons);
            auto candidate_animation = *animation;
            auto* candidate_key = candidate_animation.find_keyframe(keyframe_id);
            candidate_key->pose = candidate_pose;
            const auto keyframe_index = candidate_animation.keyframe_index(keyframe_id);
            if (!keyframe_index)
                throw std::runtime_error("animation keyframe index missing during pose edit");
            if (auto status = validate_keyframe_transition_constraints(candidate_animation,
                    *keyframe_index, proj.core_.topology(), skeletons);
                status != sm::result::success) {
                sm::apply_skeletal_pose(before_pose, proj.topology(), skeletons);
                emit proj.animation_authoring_error(QStringLiteral("This edit would violate the current animation pose constraints."));
                emit proj.refresh_canvas(proj, false);
                return status;
            }
            if (validate_pin_endpoints(candidate_animation, proj.core_.topology(), skeletons)
 != sm::result::success) {
                sm::apply_skeletal_pose(before_pose, proj.topology(), skeletons);
                emit proj.animation_authoring_error(QStringLiteral("This edit would violate the current animation pose constraints."));
                emit proj.refresh_canvas(proj, false);
                return sm::result::invalid_animation;
            }
            state->after_pose = std::move(candidate_pose);
        } else {
            sm::apply_skeletal_pose(*state->after_pose, proj.topology(), skeletons);
        }

        keyframe->pose = *state->after_pose;
        proj.animation_session_->selected_keyframe = keyframe_id;
        emit proj.animation_keyframe_selected(keyframe_id);
        emit proj.animation_preview_changed();
        emit proj.refresh_canvas(proj, false);
        return sm::result::success;
    };
    cmd.animation_edit = true;
    cmd.undo = [character_id, animation_id, keyframe_id,
            before_pose, skeletons](project& proj) {
        auto* animation = proj.core_.animation_data(character_id).find_animation(animation_id);
        auto* keyframe = animation ? animation->find_keyframe(keyframe_id) : nullptr;
        if (!keyframe)
            throw std::runtime_error("animation keyframe missing during pose undo");

        keyframe->pose = before_pose;
        sm::apply_skeletal_pose(before_pose, proj.topology(), skeletons);
        proj.animation_session_->selected_keyframe = keyframe_id;
        emit proj.animation_keyframe_selected(keyframe_id);
        emit proj.animation_preview_changed();
        emit proj.refresh_canvas(proj, false);
    };
    execute_session_command(cmd);
}

sm::result mdl::project::begin_animation_session(sm::object_id character_id, sm::object_id animation_id) {
    if (animation_session_)
        return sm::result::invalid_membership;
    auto character = core_.character(character_id);
    if (!character)
        return character.error();
    auto* source_animation = character->get().animation_data().find_animation(animation_id);
    if (!source_animation)
        return sm::result::not_found;

    animation_edit_session session;
    session.character = character_id;
    session.animation = animation_id;
    session.original_animation_data = character->get().animation_data();
    for (auto skel : character->get().rig().skeletons()) {
        auto copied = skel->copy_to(session.working_topology);
        if (!copied)
            return copied.error();
        copied->get().clear_user_data();
        for (auto node : copied->get().nodes())
            node->clear_user_data();
        for (auto bone : copied->get().bones())
            bone->clear_user_data();
    }
    if (!source_animation->keyframes.empty())
        session.selected_keyframe = source_animation->keyframes.front().id;
    // Match the existing append behavior: a newly appended transition inherits
    // the preceding transition's pins unless the user changes them on the final
    // frame first.  Unlike a real transition, this terminal state is editor-only.
    if (!source_animation->transitions.empty())
        session.terminal_pinned_nodes = source_animation->transitions.back().pinned_nodes;
    animation_session_ = std::move(session);
    if (animation_session_->selected_keyframe) {
        const auto* a = core_.animation_data(character_id).find_animation(animation_id);
        sm::apply_skeletal_pose(a->find_keyframe(*animation_session_->selected_keyframe)->pose,
            animation_session_->working_topology, character->get().rig().skeleton_ids());
    }
    emit refresh_undo_redo_state(false, false);
    emit refresh_canvas(*this, true);
    if (animation_session_->selected_keyframe)
        emit animation_keyframe_selected(*animation_session_->selected_keyframe);
    return sm::result::success;
}
void mdl::project::end_animation_session() {
    if (!animation_session_)
        return;
    exit_animation_preview();
    emit animation_session_ending();
    emit topology_about_to_reset();
    const auto character_id = animation_session_->character;
    const auto before = animation_session_->original_animation_data;
    const auto after = core_.animation_data(character_id);
    const bool changed = animation_session_->authored_depth != 0;
    animation_session_.reset();
    if (changed) {
        command cmd{
            [character_id, after](project& p) { p.core_.animation_data(character_id) = after; return sm::result::success; },
            [character_id, before](project& p) { p.core_.animation_data(character_id) = before; }
        };
        execute_command(cmd);
    } else {
        emit refresh_undo_redo_state(can_redo(), can_undo());
        emit refresh_canvas(*this, true);
    }
}
std::optional<sm::object_id> mdl::project::animation_session_character() const {
    if (!animation_session_)
        return {};
    return animation_session_->character;
}
std::optional<sm::object_id> mdl::project::animation_session_animation() const {
    if (!animation_session_)
        return {};
    return animation_session_->animation;
}
std::optional<sm::object_id> mdl::project::animation_session_keyframe() const {
    if (!animation_session_)
        return {};
    return animation_session_->selected_keyframe;
}

std::unordered_set<sm::object_id> mdl::project::animation_session_pinned_nodes() const {
    if (!animation_session_)
        return {};
    if (animation_preview_active())
        return playback_pinned_node_ids_;
    if (!animation_session_->selected_keyframe)
        return {};
    const auto* animation = core_.animation_data(animation_session_->character).find_animation(
        animation_session_->animation);
    if (!animation)
        return {};
    const auto index = animation->keyframe_index(*animation_session_->selected_keyframe);
    if (!index)
        return {};
    if (*index < animation->transitions.size())
        return animation->transitions[*index].pinned_nodes;
    if (*index + 1 == animation->keyframes.size())
        return animation_session_->terminal_pinned_nodes;
    return {};
}

sm::constraint_map mdl::project::animation_session_rotation_constraints() const {
    if (!animation_session_)
        return {};
    // While previewing playback (including a pause or scrub), the widgets belong
    // to the transition sampled at the displayed time. When preview ends, they
    // return to the selected keyframe's outgoing transition below.
    if (animation_preview_active())
        return playback_rotation_constraints_;
    if (!animation_session_->selected_keyframe)
        return {};
    const auto* animation = core_.animation_data(animation_session_->character).find_animation(
        animation_session_->animation);
    if (!animation)
        return {};
    const auto index = animation->keyframe_index(*animation_session_->selected_keyframe);
    return index && *index < animation->transitions.size()
        ? animation->transitions[*index].rotation_constraints : sm::constraint_map{};
}

std::expected<sm::constraint_map, sm::result> mdl::project::animation_edit_constraints() const {
    if (!animation_session_ || animation_preview_active() || !animation_session_->selected_keyframe)
        return topology().constraints();
    const auto* animation = core_.animation_data(animation_session_->character).find_animation(
        animation_session_->animation);
    if (!animation)
        return std::unexpected(sm::result::not_found);
    const auto index = animation->keyframe_index(*animation_session_->selected_keyframe);
    if (!index)
        return std::unexpected(sm::result::not_found);
    return effective_keyframe_constraints(*animation, *index, topology());
}

std::expected<sm::object_id, sm::result> mdl::project::add_animation_rotation_constraint(
        sm::object_id target, sm::rotation_reference reference, sm::angle_range allowed) {
    exit_animation_preview();
    if (!animation_session_ || !animation_session_->selected_keyframe)
        return std::unexpected(sm::result::not_found);
    const auto character_id = animation_session_->character;
    const auto animation_id = animation_session_->animation;
    const auto keyframe_id = *animation_session_->selected_keyframe;
    auto* animation = core_.animation_data(character_id).find_animation(animation_id);
    auto index = animation ? animation->keyframe_index(keyframe_id) : std::nullopt;
    if (!animation || !index || *index >= animation->transitions.size())
        return std::unexpected(sm::result::not_found);
    auto& transition = animation->transitions[*index];
    for (const auto& [id, c] : transition.rotation_constraints)
        if (c.rotation() && c.rotation()->target_bone == target)
            return std::unexpected(sm::result::invalid_constraint);

    sm::object_id id;
    do id = sm::object_id::generate();
    while (core_.constraint_by_id(id).has_value() || transition.rotation_constraints.contains(id));
    sm::constraint value{id, "Transition rotation constraint",
        sm::rotation_constraint{target, reference, allowed}};
    auto proposed = core_.constraints();
    proposed.emplace(id, value);
    for (const auto& [other_id, other] : transition.rotation_constraints)
        proposed.emplace(other_id, other);
    if (auto status = sm::validate_constraints(core_.topology(), proposed); status != sm::result::success)
        return std::unexpected(status);

    const auto skeletons = core_.character(character_id)->get().rig().skeleton_ids();
    const auto before = *animation;
    auto after = before;
    after.transitions[*index].rotation_constraints.emplace(id, value);
    if (auto status = enforce_transition_rotation_endpoints(
            after, *index, before, core_.topology(), skeletons);
        status != sm::result::success) {
        return std::unexpected(status);
    }

    auto apply = [character_id, animation_id, keyframe_id, skeletons](project& p,
            const sm::animation& value) {
        auto* a = p.core_.animation_data(character_id).find_animation(animation_id);
        if (!a)
            throw std::runtime_error("animation missing during transition constraint edit");
        *a = value;
        p.animation_session_->selected_keyframe = keyframe_id;
        auto* selected = a->find_keyframe(keyframe_id);
        if (!selected)
            throw std::runtime_error("selected keyframe missing during transition constraint edit");
        sm::apply_skeletal_pose(selected->pose, p.topology(), skeletons);
        emit p.animation_keyframe_selected(keyframe_id);
        emit p.animation_preview_changed();
        emit p.refresh_canvas(p, false);
    };
    command cmd{
        [apply, after](project& p) {
            apply(p, after);
            return sm::result::success;
        },
        [apply, before](project& p) {
            apply(p, before);
        }
    };
    cmd.animation_edit = true;
    if (auto status = execute_session_command(cmd); status != sm::result::success)
        return std::unexpected(status);
    return id;
}

std::optional<sm::rotation_constraint> mdl::project::animation_session_rotation_constraint(sm::object_id id) const {
    auto constraints = animation_session_rotation_constraints();
    auto it = constraints.find(id);
    if (it == constraints.end() || !it->second.rotation())
        return {};
    return *it->second.rotation();
}

sm::result mdl::project::preview_animation_rotation_constraint(
        sm::object_id id, sm::rotation_constraint definition) {
    if (!animation_session_ || animation_preview_active() || !animation_session_->selected_keyframe)
        return sm::result::not_found;
    auto* animation = core_.animation_data(animation_session_->character).find_animation(animation_session_->animation);
    auto index = animation ? animation->keyframe_index(*animation_session_->selected_keyframe) : std::nullopt;
    if (!animation || !index || *index >= animation->transitions.size())
        return sm::result::not_found;
    auto& transition = animation->transitions[*index];
    auto it = transition.rotation_constraints.find(id);
    if (it == transition.rotation_constraints.end())
        return sm::result::not_found;
    sm::constraint replacement{id, it->second.name(), definition};
    auto proposed = core_.constraints();
    for (const auto& [other_id, other] : transition.rotation_constraints)
        proposed.emplace(other_id, other_id == id ? replacement : other);
    if (auto status = sm::validate_constraints(core_.topology(), proposed); status != sm::result::success)
        return status;
    it->second = std::move(replacement);
    emit refresh_canvas(*this, false);
    return sm::result::success;
}

sm::result mdl::project::update_animation_rotation_constraint(
        sm::object_id id, sm::rotation_constraint definition) {
    exit_animation_preview();
    if (!animation_session_ || !animation_session_->selected_keyframe)
        return sm::result::not_found;
    const auto character_id = animation_session_->character;
    const auto animation_id = animation_session_->animation;
    const auto keyframe_id = *animation_session_->selected_keyframe;
    auto* animation = core_.animation_data(character_id).find_animation(animation_id);
    auto index = animation ? animation->keyframe_index(keyframe_id) : std::nullopt;
    if (!animation || !index || *index >= animation->transitions.size())
        return sm::result::not_found;
    auto& transition = animation->transitions[*index];
    auto found = transition.rotation_constraints.find(id);
    if (found == transition.rotation_constraints.end() || !found->second.rotation())
        return sm::result::not_found;
    sm::constraint replacement{id, found->second.name(), definition};
    auto proposed = core_.constraints();
    for (const auto& [other_id, other] : transition.rotation_constraints)
        proposed.emplace(other_id, other_id == id ? replacement : other);
    if (auto status = sm::validate_constraints(core_.topology(), proposed); status != sm::result::success)
        return status;

    const auto before = *animation;
    auto after = before;
    auto& after_constraint = after.transitions[*index].rotation_constraints.at(id);
    after_constraint = sm::constraint{id, after_constraint.name(), definition};
    const auto skeletons = core_.character(character_id)->get().rig().skeleton_ids();
    if (auto status = validate_keyframe_transition_constraints(
            after, *index, core_.topology(), skeletons);
        status != sm::result::success) {
        return status;
    }
    if (auto status = validate_keyframe_transition_constraints(
            after, *index + 1, core_.topology(), skeletons);
        status != sm::result::success) {
        return status;
    }

    auto apply = [character_id, animation_id, keyframe_id, skeletons](project& p,
            const sm::animation& value) {
        auto* a = p.core_.animation_data(character_id).find_animation(animation_id);
        if (!a)
            throw std::runtime_error("animation missing during constraint edit");
        *a = value;
        p.animation_session_->selected_keyframe = keyframe_id;
        auto* selected = a->find_keyframe(keyframe_id);
        if (!selected)
            throw std::runtime_error("selected keyframe missing during constraint edit");
        sm::apply_skeletal_pose(selected->pose, p.topology(), skeletons);
        emit p.animation_keyframe_selected(keyframe_id);
        emit p.animation_preview_changed();
        emit p.refresh_canvas(p, false);
    };
    command cmd{
        [apply, after](project& p) {
            apply(p, after);
            return sm::result::success;
        },
        [apply, before](project& p) {
            apply(p, before);
        }
    };
    cmd.animation_edit = true;
    return execute_session_command(cmd);
}

sm::result mdl::project::remove_animation_rotation_constraint(sm::object_id id) {
    exit_animation_preview();
    if (!animation_session_ || !animation_session_->selected_keyframe)
        return sm::result::not_found;
    const auto character_id = animation_session_->character;
    const auto animation_id = animation_session_->animation;
    const auto keyframe_id = *animation_session_->selected_keyframe;
    auto* animation = core_.animation_data(character_id).find_animation(animation_id);
    auto index = animation ? animation->keyframe_index(keyframe_id) : std::nullopt;
    if (!animation || !index || *index >= animation->transitions.size())
        return sm::result::not_found;
    auto& transition = animation->transitions[*index];
    auto it = transition.rotation_constraints.find(id);
    if (it == transition.rotation_constraints.end())
        return sm::result::not_found;
    const auto saved = it->second;
    const auto transition_id = transition.id;
    auto apply = [character_id, animation_id, keyframe_id, transition_id, saved](project& p, bool present) {
        auto* a = p.core_.animation_data(character_id).find_animation(animation_id);
        if (!a)
            throw std::runtime_error("animation missing during constraint removal");
        auto tr = std::ranges::find_if(a->transitions, [ = ](const auto& x){ return x.id == transition_id; });
        if (tr == a->transitions.end())
            throw std::runtime_error("transition missing during constraint removal");
        if (present)
            tr->rotation_constraints.insert_or_assign(saved.id(), saved);
        else
            tr->rotation_constraints.erase(saved.id());
        p.animation_session_->selected_keyframe = keyframe_id;
        emit p.animation_preview_changed();
        emit p.refresh_canvas(p, false);
    };
    command cmd{
        [apply](project& p) {
            apply(p, false);
            return sm::result::success;
        },
        [apply](project& p) {
            apply(p, true);
        }
    };
    cmd.animation_edit = true;
    return execute_session_command(cmd);
}

std::unordered_set<sm::object_id> mdl::project::animation_session_incoming_locked_nodes() const {
    if (!animation_session_ || animation_preview_active() || !animation_session_->selected_keyframe)
        return {};
    const auto* animation = core_.animation_data(animation_session_->character).find_animation(animation_session_->animation);
    if (!animation)
        return {};
    auto index = animation->keyframe_index(*animation_session_->selected_keyframe);
    if (!index || *index == 0)
        return {};
    return animation->transitions[*index - 1].pinned_nodes;
}

sm::constraint_map mdl::project::animation_session_incoming_rotation_constraints() const {
    if (!animation_session_ || animation_preview_active() || !animation_session_->selected_keyframe)
        return {};
    const auto* animation = core_.animation_data(animation_session_->character).find_animation(
        animation_session_->animation);
    if (!animation)
        return {};
    const auto index = animation->keyframe_index(*animation_session_->selected_keyframe);
    if (!index || *index == 0)
        return {};
    return animation->transitions[*index - 1].rotation_constraints;
}

std::optional<std::string> mdl::project::animation_session_incoming_lock_source_label(sm::object_id node) const {
    if (!animation_session_ || !animation_session_->selected_keyframe)
        return {};
    const auto* animation = core_.animation_data(animation_session_->character).find_animation(animation_session_->animation);
    if (!animation)
        return {};
    auto index = animation->keyframe_index(*animation_session_->selected_keyframe);
    if (!index || *index == 0 || !animation->transitions[*index - 1].pinned_nodes.contains(node))
        return {};
    const auto& source = animation->keyframes[*index - 1];
    if (source.name)
        return *source.name;
    return std::string("Pose ") + std::to_string(*index);
}

sm::result mdl::project::set_animation_outgoing_transition_node_pinned(sm::object_id node_id, bool pinned) {
    if (!animation_session_ || !animation_session_->selected_keyframe)
        return sm::result::not_found;
    if (!animation_session_->working_topology.get<sm::node>(node_id))
        return sm::result::invalid_membership;

    const auto character_id = animation_session_->character;
    const auto animation_id = animation_session_->animation;
    const auto keyframe_id = *animation_session_->selected_keyframe;
    auto* animation = core_.animation_data(character_id).find_animation(animation_id);
    if (!animation)
        return sm::result::not_found;
    const auto index = animation->keyframe_index(keyframe_id);
    if (!index)
        return sm::result::not_found;

    // The final keyframe has no outgoing Core transition.  Pins there are still
    // useful to the pose editor/IK solver, so store them as an undoable piece of
    // animation-session state.  They become the pins of the transition when a
    // new keyframe is appended or the final keyframe is duplicated.
    if (*index == animation->transitions.size()) {
        if (*index + 1 != animation->keyframes.size())
            return sm::result::invalid_animation;
        const bool before = animation_session_->terminal_pinned_nodes.contains(node_id);
        if (before == pinned)
            return sm::result::success;
        auto apply = [keyframe_id, node_id](project& p, bool value) {
            if (value)
                p.animation_session_->terminal_pinned_nodes.insert(node_id);
            else
                p.animation_session_->terminal_pinned_nodes.erase(node_id);
            p.animation_session_->selected_keyframe = keyframe_id;
            emit p.animation_preview_changed();
            emit p.refresh_canvas(p, false);
        };
        command cmd{
            [apply, pinned](project& p) { apply(p, pinned); return sm::result::success; },
            [apply, before](project& p) { apply(p, before); }
        };
        // This state is intentionally not persisted unless/until an actual
        // transition is created, so it must not make the document dirty by itself.
        cmd.animation_edit = false;
        return execute_session_command(cmd);
    }
    if (*index > animation->transitions.size())
        return sm::result::invalid_animation;

    auto& transition = animation->transitions[*index];
    const auto transition_id = transition.id;
    const bool before = transition.pinned_nodes.contains(node_id);
    if (before == pinned)
        return sm::result::success;
    if (pinned) {
        const auto skeletons = core_.character(character_id)->get().rig().skeleton_ids();
        auto a = pose_node_position(animation->keyframes[*index].pose, core_.topology(), skeletons, node_id);
        auto b = pose_node_position(animation->keyframes[*index + 1].pose, core_.topology(), skeletons, node_id);
        if (!a || !b || sm::distance(*a, *b) > k_animation_pin_position_tolerance) {
            const auto& source = animation->keyframes[*index];
            const auto& destination = animation->keyframes[*index + 1];
            const auto source_label = source.name ? QString::fromStdString(*source.name) :
                QStringLiteral("Pose %1").arg(*index + 1);
            const auto destination_label = destination.name ? QString::fromStdString(*destination.name) :
                QStringLiteral("Pose %1").arg(*index + 2);
            emit animation_authoring_error(QStringLiteral("Cannot pin this node in %1: its position differs in %2.")
                .arg(source_label, destination_label));
            return sm::result::invalid_animation;
        }
    }

    auto apply = [character_id, animation_id, keyframe_id, transition_id, node_id](project& p, bool value) {
        auto* animation = p.core_.animation_data(character_id).find_animation(animation_id);
        if (!animation)
            throw std::runtime_error("animation missing during transition pin edit");
        auto transition = std::find_if(animation->transitions.begin(), animation->transitions.end(),
            [transition_id](const auto& candidate) { return candidate.id == transition_id; });
        if (transition == animation->transitions.end())
            throw std::runtime_error("animation transition missing during pin edit");
        if (value)
            transition->pinned_nodes.insert(node_id);
        else
            transition->pinned_nodes.erase(node_id);
        p.animation_session_->selected_keyframe = keyframe_id;
        emit p.animation_preview_changed();
        emit p.refresh_canvas(p, false);
    };

    command cmd{
        [apply, pinned](project& p) { apply(p, pinned); return sm::result::success; },
        [apply, before](project& p) { apply(p, before); }
    };
    cmd.animation_edit = true;
    return execute_session_command(cmd);
}

sm::result mdl::project::select_animation_keyframe(sm::object_id keyframe_id) {
    exit_animation_preview();
    if (!animation_session_)
        return sm::result::invalid_membership;
    auto character = core_.character(animation_session_->character);
    if (!character)
        return character.error();
    auto* a = core_.animation_data(animation_session_->character).find_animation(animation_session_->animation);
    auto* k = a ? a->find_keyframe(keyframe_id) : nullptr;
    if (!k)
        return sm::result::not_found;
    sm::apply_skeletal_pose(k->pose, animation_session_->working_topology, character->get().rig().skeleton_ids());
    animation_session_->selected_keyframe = keyframe_id;
    emit animation_keyframe_selected(keyframe_id);
    emit refresh_canvas(*this, false);
    emit animation_preview_changed();
    return sm::result::success;
}

sm::result mdl::project::add_animation_keyframe() {
    exit_animation_preview();
    if (!animation_session_)
        return sm::result::invalid_membership;

    const auto character_id = animation_session_->character;
    const auto animation_id = animation_session_->animation;
    auto* animation = core_.animation_data(character_id).find_animation(animation_id);
    if (!animation)
        return sm::result::not_found;

    const auto skeletons = core_.character(character_id)->get().rig().skeleton_ids();
    const auto before = *animation;
    auto after = before;

    sm::pose_keyframe keyframe;
    keyframe.pose = after.keyframes.empty() ?
        sm::capture_skeletal_pose(topology(), skeletons) : after.keyframes.back().pose;
    const auto keyframe_id = keyframe.id;
    if (!after.keyframes.empty()) {
        sm::pose_transition transition;
        // Pins on the terminal frame describe this not-yet-created interval.
        // Rotation constraints still use the existing carry-forward behavior.
        transition.pinned_nodes = animation_session_->terminal_pinned_nodes;
        if (!after.transitions.empty()) {
            transition.rotation_constraints = clone_transition_rotation_constraints(
                after.transitions.back().rotation_constraints);
        }
        after.transitions.push_back(std::move(transition));
    }
    after.keyframes.push_back(std::move(keyframe));
    if (validate_pin_endpoints(after, core_.topology(), skeletons) != sm::result::success) {
        emit animation_authoring_error(QStringLiteral("Cannot add this pose because it would create mismatched pinned endpoints."));
        return sm::result::invalid_animation;
    }

    auto apply = [character_id, animation_id, keyframe_id, skeletons](
            project& p, const sm::animation& value) {
        auto* target = p.core_.animation_data(character_id).find_animation(animation_id);
        if (!target)
            throw std::runtime_error("animation missing");

        *target = value;
        p.animation_session_->selected_keyframe = keyframe_id;
        sm::apply_skeletal_pose(target->find_keyframe(keyframe_id)->pose,
            p.topology(), skeletons);
        emit p.animation_keyframe_selected(keyframe_id);
        emit p.animation_preview_changed();
        emit p.refresh_canvas(p, false);
    };

    command cmd;
    cmd.redo = [after, apply](project& p) {
        apply(p, after);
        return sm::result::success;
    };
    cmd.animation_edit = true;
    cmd.undo = [before, character_id, animation_id, skeletons](project& p) {
        auto* target = p.core_.animation_data(character_id).find_animation(animation_id);
        if (!target)
            throw std::runtime_error("animation missing");
        *target = before;

        if (!before.keyframes.empty()) {
            const auto previous_id = before.keyframes.back().id;
            p.animation_session_->selected_keyframe = previous_id;
            sm::apply_skeletal_pose(before.keyframes.back().pose, p.topology(), skeletons);
            emit p.animation_keyframe_selected(previous_id);
        } else {
            p.animation_session_->selected_keyframe.reset();
        }
        emit p.animation_preview_changed();
        emit p.refresh_canvas(p, false);
    };
    return execute_session_command(cmd);
}

sm::result mdl::project::duplicate_animation_keyframe() {
    exit_animation_preview();
    if (!animation_session_ || !animation_session_->selected_keyframe) {
        return sm::result::not_found;
    }

    const auto character_id = animation_session_->character;
    const auto animation_id = animation_session_->animation;
    const auto selected_id = *animation_session_->selected_keyframe;
    auto* animation = core_.animation_data(character_id).find_animation(animation_id);
    if (!animation)
        return sm::result::not_found;

    auto index = animation->keyframe_index(selected_id);
    if (!index)
        return sm::result::not_found;

    const auto before = *animation;
    auto after = before;
    auto copy = after.keyframes[*index];
    copy.id = sm::object_id::generate();
    const auto copy_id = copy.id;
    after.keyframes.insert(after.keyframes.begin() + *index + 1, std::move(copy));

    if (*index < after.transitions.size()) {
        sm::pose_transition inserted;
        inserted.pinned_nodes = after.transitions[*index].pinned_nodes;
        inserted.rotation_constraints = clone_transition_rotation_constraints(
            after.transitions[*index].rotation_constraints);
        after.transitions.insert(after.transitions.begin() + *index, std::move(inserted));
    } else {
        sm::pose_transition appended;
        appended.pinned_nodes = animation_session_->terminal_pinned_nodes;
        if (!after.transitions.empty()) {
            appended.rotation_constraints = clone_transition_rotation_constraints(
                after.transitions.back().rotation_constraints);
        }
        after.transitions.push_back(std::move(appended));
    }

    const auto skeletons = core_.character(character_id)->get().rig().skeleton_ids();
    if (validate_pin_endpoints(after, core_.topology(), skeletons) != sm::result::success) {
        emit animation_authoring_error(QStringLiteral("Cannot duplicate this pose because it would create mismatched pinned endpoints."));
        return sm::result::invalid_animation;
    }
    command cmd{
        [character_id, animation_id, copy_id, after, skeletons](project& p) {
            auto* target = p.core_.animation_data(character_id).find_animation(animation_id);
            if (!target)
                throw std::runtime_error("animation missing");
            *target = after;
            p.animation_session_->selected_keyframe = copy_id;
            sm::apply_skeletal_pose(target->find_keyframe(copy_id)->pose,
                p.topology(), skeletons);
            emit p.animation_keyframe_selected(copy_id);
            emit p.animation_preview_changed();
            emit p.refresh_canvas(p, false);
            return sm::result::success;
        },
        [character_id, animation_id, selected_id, before, skeletons](project& p) {
            auto* target = p.core_.animation_data(character_id).find_animation(animation_id);
            if (!target)
                throw std::runtime_error("animation missing");
            *target = before;
            p.animation_session_->selected_keyframe = selected_id;
            sm::apply_skeletal_pose(target->find_keyframe(selected_id)->pose,
                p.topology(), skeletons);
            emit p.animation_keyframe_selected(selected_id);
            emit p.animation_preview_changed();
            emit p.refresh_canvas(p, false);
        }
    };
    cmd.animation_edit = true;
    return execute_session_command(cmd);
}

sm::result mdl::project::set_animation_transition_duration(sm::object_id transition_id, double seconds) {
    if (!animation_session_)
        return sm::result::invalid_membership;
    if (!(seconds > 0.0) || !std::isfinite(seconds))
        return sm::result::invalid_animation;
    const auto character_id = animation_session_->character;
    const auto animation_id = animation_session_->animation;
    auto* animation = core_.animation_data(character_id).find_animation(animation_id);
    if (!animation)
        return sm::result::not_found;
    auto it = std::find_if(animation->transitions.begin(), animation->transitions.end(),
        [transition_id](const auto& t) { return t.id == transition_id; });
    if (it == animation->transitions.end())
        return sm::result::not_found;
    const double before = it->duration_seconds;
    if (before == seconds)
        return sm::result::success;
    double total = animation->duration_seconds() - before + seconds;
    if (!std::isfinite(total))
        return sm::result::invalid_animation;
    auto apply = [character_id, animation_id, transition_id](project& p, double value) {
        auto* a = p.core_.animation_data(character_id).find_animation(animation_id);
        if (!a)
            throw std::runtime_error("animation missing");
        auto t = std::find_if(a->transitions.begin(), a->transitions.end(),
            [transition_id](const auto& candidate) { return candidate.id == transition_id; });
        if (t == a->transitions.end())
            throw std::runtime_error("transition missing");
        t->duration_seconds = value;
        emit p.animation_preview_changed();
    };
    command cmd{
        [apply, seconds](project& p) {
            apply(p, seconds);
            return sm::result::success;
        },
        [apply, before](project& p) {
            apply(p, before);
        }
    };
    cmd.animation_edit = true;
    return execute_session_command(cmd);
}

sm::result mdl::project::insert_animation_keyframe(double seconds) {
    if (!animation_session_ || !std::isfinite(seconds))
        return sm::result::invalid_animation;
    const auto character_id = animation_session_->character;
    const auto animation_id = animation_session_->animation;
    auto* animation = core_.animation_data(character_id).find_animation(animation_id);
    if (!animation || animation->keyframes.size() < 2)
        return sm::result::invalid_animation;
    const auto skeletons = core_.character(character_id)->get().rig().skeleton_ids();
    if (validate_pin_endpoints(*animation, core_.topology(), skeletons) != sm::result::success) {
        emit animation_authoring_error(QStringLiteral("Cannot insert into a transition with mismatched pinned endpoints. Remove the offending transition pin first."));
        return sm::result::invalid_animation;
    }

    double start = 0.0;
    std::optional<std::size_t> interval;
    for (std::size_t i = 0; i < animation->transitions.size(); ++i) {
        const double end = start + animation->transitions[i].duration_seconds;
        if (seconds > start && seconds < end) {
            interval = i;
            break;
        }
        start = end;
    }
    if (!interval)
        return sm::result::invalid_animation;
    const double original_duration = animation->transitions[*interval].duration_seconds;
    const double first_duration = seconds - start;
    const double second_duration = original_duration - first_duration;
    const double duration_tolerance = std::max(1e-12, std::abs(original_duration) * 1e-12);
    if (!(first_duration > 0.0) || !(second_duration > 0.0) ||
            !std::isfinite(first_duration) || !std::isfinite(second_duration) ||
            std::abs((first_duration + second_duration) - original_duration) > duration_tolerance)
        return sm::result::invalid_animation;

    auto sampled = sm::sample_constrained_pose(*animation, seconds, core_.topology(), skeletons,
        core_.character(animation_session_->character)->get().character_root_bone());
    if (!sampled || !*sampled || !std::holds_alternative<sm::reference_transition>((**sampled).location))
        return sampled ? sm::result::invalid_animation : sampled.error();

    const auto before = *animation;
    auto after = before;
    sm::pose_keyframe inserted;
    inserted.pose = (**sampled).pose;
    const auto inserted_id = inserted.id;
    const auto split_pins = after.transitions[*interval].pinned_nodes;
    const auto split_rotation_constraints = after.transitions[*interval].rotation_constraints;
    after.keyframes.insert(after.keyframes.begin() + *interval + 1, inserted);
    after.transitions[*interval].duration_seconds = first_duration; // preserve original ID and pins
    sm::pose_transition second;
    second.duration_seconds = second_duration;
    second.pinned_nodes = split_pins;
    second.rotation_constraints = clone_transition_rotation_constraints(split_rotation_constraints);
    after.transitions.insert(after.transitions.begin() + *interval + 1, std::move(second));
    if (validate_pin_endpoints(after, core_.topology(), skeletons) != sm::result::success) {
        emit animation_authoring_error(QStringLiteral("The sampled pose cannot be inserted without violating pinned endpoints."));
        return sm::result::invalid_animation;
    }

    const auto previous_selection = animation_session_->selected_keyframe;
    auto apply = [character_id, animation_id, skeletons](project& p, const sm::animation& value,
            std::optional<sm::object_id> selection) {
        auto* a = p.core_.animation_data(character_id).find_animation(animation_id);
        if (!a)
            throw std::runtime_error("animation missing");
        *a = value;
        p.animation_session_->selected_keyframe = selection;
        if (selection) {
            auto* k = a->find_keyframe(*selection);
            if (!k)
                throw std::runtime_error("keyframe missing");
            sm::apply_skeletal_pose(k->pose, p.topology(), skeletons);
            emit p.animation_keyframe_selected(*selection);
        }
        emit p.animation_preview_changed();
        emit p.refresh_canvas(p, false);
    };
    command cmd{
        [apply, after, inserted_id](project& p) {
            apply(p, after, inserted_id);
            return sm::result::success;
        },
        [apply, before, previous_selection](project& p) {
            apply(p, before, previous_selection);
        }
    };
    cmd.animation_edit = true;
    return execute_session_command(cmd);
}

sm::result mdl::project::rename_animation_keyframe(const std::optional<std::string>& name) {
    exit_animation_preview();
    if (!animation_session_ || !animation_session_->selected_keyframe) {
        return sm::result::not_found;
    }

    const auto character_id = animation_session_->character;
    const auto animation_id = animation_session_->animation;
    const auto keyframe_id = *animation_session_->selected_keyframe;
    auto* animation = core_.animation_data(character_id).find_animation(animation_id);
    auto* keyframe = animation ? animation->find_keyframe(keyframe_id) : nullptr;
    if (!keyframe)
        return sm::result::not_found;

    const auto old_name = keyframe->name;
    command cmd{
        [character_id, animation_id, keyframe_id, name](project& p) {
            auto* target = p.core_.animation_data(character_id).find_animation(animation_id);
            if (!target)
                throw std::runtime_error("animation missing");
            auto* keyframe = target->find_keyframe(keyframe_id);
            if (!keyframe)
                throw std::runtime_error("animation keyframe missing");
            keyframe->name = name;
            p.animation_session_->selected_keyframe = keyframe_id;
            emit p.animation_keyframe_selected(keyframe_id);
            emit p.animation_preview_changed();
            return sm::result::success;
        },
        [character_id, animation_id, keyframe_id, old_name](project& p) {
            auto* target = p.core_.animation_data(character_id).find_animation(animation_id);
            if (!target)
                throw std::runtime_error("animation missing");
            auto* keyframe = target->find_keyframe(keyframe_id);
            if (!keyframe)
                throw std::runtime_error("animation keyframe missing");
            keyframe->name = old_name;
            p.animation_session_->selected_keyframe = keyframe_id;
            emit p.animation_keyframe_selected(keyframe_id);
            emit p.animation_preview_changed();
        }
    };
    cmd.animation_edit = true;
    return execute_session_command(cmd);
}

sm::result mdl::project::delete_animation_keyframe() {
    exit_animation_preview();
    if (!animation_session_ || !animation_session_->selected_keyframe) {
        return sm::result::not_found;
    }

    const auto character_id = animation_session_->character;
    const auto animation_id = animation_session_->animation;
    const auto keyframe_id = *animation_session_->selected_keyframe;
    auto* animation = core_.animation_data(character_id).find_animation(animation_id);
    if (!animation)
        return sm::result::not_found;

    auto index = animation->keyframe_index(keyframe_id);
    if (!index)
        return sm::result::not_found;

    const auto before = *animation;
    auto after = before;
    std::optional<std::size_t> resulting_transition_index;
    after.keyframes.erase(after.keyframes.begin() + *index);
    if (!after.transitions.empty()) {
        if (*index == 0) {
            after.transitions.erase(after.transitions.begin());
        } else if (*index == before.keyframes.size() - 1) {
            after.transitions.erase(after.transitions.begin() + (*index - 1));
        } else {
            // Carry the removed interval's constraints onto the joined transition.
            const auto resulting_pins = after.transitions[*index - 1].pinned_nodes;
            const auto resulting_constraints = after.transitions[*index - 1].rotation_constraints;
            after.transitions.erase(after.transitions.begin() + (*index - 1));
            after.transitions[*index - 1].pinned_nodes = resulting_pins;
            after.transitions[*index - 1].rotation_constraints = resulting_constraints;
            resulting_transition_index = *index - 1;
        }
    }

    std::optional<sm::object_id> next;
    if (!after.keyframes.empty()) {
        next = after.keyframes[std::min(*index, after.keyframes.size() - 1)].id;
    }

    const auto skeletons = core_.character(character_id)->get().rig().skeleton_ids();
    if (resulting_transition_index &&
            !after.transitions[*resulting_transition_index].rotation_constraints.empty()) {
        if (auto status = validate_keyframe_transition_constraints(after,
                *resulting_transition_index, core_.topology(), skeletons);
            status != sm::result::success) {
            emit animation_authoring_error(QStringLiteral("Cannot delete this pose because the resulting transition constraints are infeasible."));
            return status;
        }
        if (auto status = validate_keyframe_transition_constraints(after,
                *resulting_transition_index + 1, core_.topology(), skeletons);
            status != sm::result::success) {
            emit animation_authoring_error(QStringLiteral("Cannot delete this pose because the resulting transition constraints are infeasible."));
            return status;
        }
    }
    if (validate_pin_endpoints(after, core_.topology(), skeletons) != sm::result::success) {
        emit animation_authoring_error(QStringLiteral("Cannot delete this pose because it would create mismatched pinned endpoints."));
        return sm::result::invalid_animation;
    }
    command cmd{
        [character_id, animation_id, after, next, skeletons](project& p) {
            auto* target = p.core_.animation_data(character_id).find_animation(animation_id);
            if (!target)
                throw std::runtime_error("animation missing");
            *target = after;
            p.animation_session_->selected_keyframe = next;
            if (next) {
                sm::apply_skeletal_pose(target->find_keyframe(*next)->pose,
                    p.topology(), skeletons);
                emit p.animation_keyframe_selected(*next);
            }
            emit p.animation_preview_changed();
            emit p.refresh_canvas(p, false);
            return sm::result::success;
        },
        [character_id, animation_id, before, keyframe_id, skeletons](project& p) {
            auto* target = p.core_.animation_data(character_id).find_animation(animation_id);
            if (!target)
                throw std::runtime_error("animation missing");
            *target = before;
            p.animation_session_->selected_keyframe = keyframe_id;
            sm::apply_skeletal_pose(target->find_keyframe(keyframe_id)->pose,
                p.topology(), skeletons);
            emit p.animation_keyframe_selected(keyframe_id);
            emit p.animation_preview_changed();
            emit p.refresh_canvas(p, false);
        }
    };
    cmd.animation_edit = true;
    return execute_session_command(cmd);
}

void mdl::project::set_show_previous_pose(bool show) {
    if (show_previous_pose_ == show)
        return;
    show_previous_pose_ = show;
    emit refresh_canvas(*this, false);
}

void mdl::project::edit_animation_data(sm::object_id id, const std::function<void(sm::animation_assets&)>& edit) {
    auto before = core_.animation_data(id), after = before;
    edit(after);
    const auto character = core_.character(id);
    if (!character)
        throw std::invalid_argument("Missing character");
    after.validate(core_.topology(), character->get().rig().skeleton_ids());
    execute_command({[id, after](project& p) { p.core_.animation_data(id) = after; return sm::result::success; },
        [id, before](project& p) { p.core_.animation_data(id) = before; }});
}

void mdl::project::apply_pose(sm::object_id character, sm::object_id id) {
    if (animation_mode())
        return;
    const auto& c = core_.character(character).value().get();
    const auto* pose = c.animation_data().find_pose(id);
    if (!pose || !sm::pose_compatible(*pose, topology(), c.rig().skeleton_ids()))
        throw std::invalid_argument("Pose does not match the current rig. Update or recreate it first.");
    node_locs before, after;
    for (const auto& [nid, pt] : pose->node_positions) {
        before.emplace_back(nid, topology().get<sm::node>(nid)->get().world_pos());
        after.emplace_back(nid, pt);
    }
    transform_node_positions(before, after);
}

// A path edit is one session command; drag previews stay entirely in the editor.
// Unlike terminal pins, paths never create an implicit outgoing transition.
bool mdl::project::animation_has_outgoing_transition() const {
    if (!animation_session_ || animation_preview_active() || !animation_session_->selected_keyframe)
        return false;
    const auto* a = core_.animation_data(animation_session_->character).find_animation(animation_session_->animation);
    const auto i = a ? a->keyframe_index(*animation_session_->selected_keyframe) : std::nullopt;
    return i && *i < a->transitions.size();
}

// Paths are transition-local display widgets, like rotation constraints.
// Editing uses the selected keyframe's outgoing transition; playback and scrub
// use the sampled frame's effective transition.  Editing APIs still reject
// changes during playback via animation_has_outgoing_transition().
std::optional<std::size_t> mdl::project::animation_display_transition_index() const {
    if (!animation_session_)
        return {};
    const auto* a = core_.animation_data(animation_session_->character).find_animation(
        animation_session_->animation);
    if (!a)
        return {};
    if (animation_preview_active()) {
        if (!playback_transition_id_)
            return {};
        const auto it = std::find_if(a->transitions.begin(), a->transitions.end(),
            [this](const sm::pose_transition& tr) { return tr.id == *playback_transition_id_; });
        return it == a->transitions.end() ? std::nullopt
            : std::optional<std::size_t>(std::distance(a->transitions.begin(), it));
    }
    if (!animation_session_->selected_keyframe)
        return {};
    const auto index = a->keyframe_index(*animation_session_->selected_keyframe);
    return index && *index < a->transitions.size() ? index : std::nullopt;
}

std::vector<sm::object_id> mdl::project::animation_session_path_nodes() const {
    std::vector<sm::object_id> ids;
    const auto index = animation_display_transition_index();
    if (!index)
        return ids;
    const auto* a = core_.animation_data(animation_session_->character).find_animation(animation_session_->animation);
    for (const auto& [id, path] : a->transitions[*index].paths)
        ids.push_back(id);
    return ids;
}

std::optional<mdl::project::animation_path_context> mdl::project::animation_session_path_context(sm::object_id node) const {
    const auto index = animation_display_transition_index();
    if (!index)
        return {};
    const auto* a = core_.animation_data(animation_session_->character).find_animation(animation_session_->animation);
    const auto i = *index;
    const auto* c = &a->transitions[i];
    auto char_ref = core_.character(animation_session_->character);
    if (!char_ref)
        return {};
    const auto skeletons = char_ref->get().rig().skeleton_ids();
    auto frame = sm::fixed_animation_root(*a, core_.topology(), skeletons, char_ref->get().character_root_bone());
    auto start = sm::animation_pose_node(a->keyframes[i].pose, core_.topology(), skeletons, node);
    auto end = sm::animation_pose_node(a->keyframes[i + 1].pose, core_.topology(), skeletons, node);
    if (!frame || !start || !end)
        return {};
    const auto it = c->paths.find(node);
    sm::animation_path path;
    if (it != c->paths.end())
        path = it->second;
    path.node = node;
    return animation_path_context{std::move(path), frame->to_local(*start), frame->to_local(*end), *frame};
}

sm::result mdl::project::set_animation_path(sm::object_id node, std::optional<sm::animation_path> path) {
    if (!animation_has_outgoing_transition())
        return sm::result::invalid_animation;
    const auto cid = animation_session_->character;
    const auto aid = animation_session_->animation;
    const auto kid = *animation_session_->selected_keyframe;
    auto* a = core_.animation_data(cid).find_animation(aid);
    const auto i = *a->keyframe_index(kid);
    if (!animation_session_->working_topology.get<sm::node>(node))
        return sm::result::invalid_membership;
    if (path && path->node != node)
        return sm::result::invalid_constraint;
    if (path) {
        if (!path->is_smooth())
            return sm::result::invalid_constraint;
        for (const auto& knot : path->knots)
            for (auto p : {knot.position, knot.handle_in, knot.handle_out})
                if (!std::isfinite(p.x) || !std::isfinite(p.y))
                    return sm::result::out_of_bounds;
    }
    // A path and a hard pin at the same node have incompatible meanings.
    if (path && a->transitions[i].pinned_nodes.contains(node))
        return sm::result::invalid_constraint;
    const auto before = a->transitions[i].paths;
    auto after = before;
    if (path)
        after.insert_or_assign(node, std::move(*path));
    else
        after.erase(node);
    auto apply = [cid, aid, kid, i](project& p, const std::map<sm::object_id, sm::animation_path>& paths) {
        auto* animation = p.core_.animation_data(cid).find_animation(aid);
        if (!animation || i >= animation->transitions.size() ||
            animation->keyframes[i].id != kid)
            throw std::runtime_error("animation transition missing during path undo/redo");
        animation->transitions[i].paths = paths;
        if (p.animation_session_->selected_keyframe != kid) {
            p.animation_session_->selected_keyframe = kid;
            const auto rig = p.core_.character(cid)->get().rig().skeleton_ids();
            sm::apply_skeletal_pose(animation->keyframes[i].pose, p.topology(), rig);
            emit p.animation_keyframe_selected(kid);
        }
        emit p.animation_preview_changed();
        emit p.refresh_canvas(p, false);
    };
    command cmd{
        [apply, after](project& p) {
            apply(p, after);
            return sm::result::success;
        },
        [apply, before](project& p) {
            apply(p, before);
        }
    };
    cmd.animation_edit = true;
    return execute_session_command(cmd);
}
