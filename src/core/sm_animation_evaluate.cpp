#include "sm_animation.hpp"
#include "sm_skeleton.hpp"
#include "sm_fabrik.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace {
    bool valid_pivot(sm::rotation_pivot pivot) {
        return pivot == sm::rotation_pivot::root || pivot == sm::rotation_pivot::tip;
    }
    bool valid_propagation(sm::rotation_propagation propagation) {
        return propagation == sm::rotation_propagation::hierarchy ||
            propagation == sm::rotation_propagation::bone_only;
    }
    bool valid_reference(sm::translation_reference reference) {
        return reference==sm::translation_reference::animation_root ||
            reference==sm::translation_reference::character_root ||
            reference==sm::translation_reference::bone;
    }

    std::vector<const sm::animation_action*> ordinary_action_order(const sm::animation& animation) {
        std::vector<const sm::animation_action*> ordered;
        for (const auto& layer : animation.layers) {
            std::vector<const sm::animation_action*> actions;
            actions.reserve(layer.actions.size());
            for (const auto& action : layer.actions) actions.push_back(&action);
            std::ranges::stable_sort(actions, {}, [](auto action) { return action->start; });
            ordered.insert(ordered.end(), actions.begin(), actions.end());
        }
        return ordered;
    }

    double absolute_progress(const sm::animation_action& action, sm::animation_time time) {
        if (time <= action.start) return 0.0;
        if (time >= action.start + action.duration) return 1.0;
        return double(time - action.start) / double(action.duration);
    }
}

sm::point sm::reference_frame::vector_to_world(point local) const {
    const double c=std::cos(angle),s=std::sin(angle);
    return {c*local.x-s*local.y,s*local.x+c*local.y};
}

sm::point sm::reference_frame::vector_to_local(point world) const {
    const double c=std::cos(angle),s=std::sin(angle);
    return {c*world.x+s*world.y,-s*world.x+c*world.y};
}

sm::point sm::reference_frame::local_to_world(point local) const {
    return origin+vector_to_world(local);
}

sm::point sm::reference_frame::world_to_local(point world) const {
    return vector_to_local(world-origin);
}

std::optional<sm::reference_frame> sm::bone_reference_frame(object_id bone_id,const topology& topology) {
    auto bone=topology.get<sm::bone>(bone_id); if(!bone) return {};
    const auto origin=bone->get().parent_node().world_pos();
    const auto tip=bone->get().child_node().world_pos();
    return reference_frame{origin,sm::angle_from_u_to_v(origin,tip)};
}

std::optional<sm::reference_frame> sm::translation_reference_frame(translation_reference reference,
        object_id reference_bone,object_id character_root_bone,const pose& base,const topology& working) {
    if(!valid_reference(reference)) return {};
    if(reference==translation_reference::animation_root) {
        auto bone=working.get<sm::bone>(character_root_bone); if(!bone) return {};
        const auto u=base.node_positions.find(bone->get().parent_node().id());
        const auto v=base.node_positions.find(bone->get().child_node().id());
        if(u==base.node_positions.end() || v==base.node_positions.end()) return {};
        return reference_frame{u->second,sm::angle_from_u_to_v(u->second,v->second)};
    }
    return bone_reference_frame(reference==translation_reference::character_root ? character_root_bone : reference_bone,working);
}

std::vector<sm::object_id> sm::animation_evaluation_order(const animation& animation) {
    animation.duration();
    auto ordered = ordinary_action_order(animation);
    std::vector<object_id> ids;
    ids.reserve(ordered.size());
    for (const auto* action : ordered) ids.push_back(action->id);
    return ids;
}

sm::animation_evaluation sm::evaluate_animation(const animation& animation, const pose& base,
        object_id character_root_bone, topology& working, animation_time time) {
    animation.duration(); // Validate intervals before touching the working pose.
    for (auto s : working.skeletons()) for (auto n : s->nodes())
        if (!base.node_positions.contains(n->id())) throw std::invalid_argument("Incomplete animation base pose");

    const auto ordered = ordinary_action_order(animation);
    apply_pose(base, working);
    animation_evaluation report;
    report.evaluation_order.reserve(ordered.size());

    for (const auto* action : ordered) {
        report.evaluation_order.push_back(action->id);
        auto& context = report.contexts[action->id];
        const double progress = absolute_progress(*action, time);
        const double eased = ease(action->easing, progress);

        if (const auto* rotation = std::get_if<rigid_rotation>(&action->data)) {
            auto bone = working.get<sm::bone>(rotation->bone);
            if (!bone || !std::isfinite(rotation->angle) || !valid_pivot(rotation->pivot) ||
                !valid_propagation(rotation->propagation)) {
                report.invalid_actions.push_back(action->id);
                continue;
            }
            auto& pivot = rotation->pivot == rotation_pivot::root ?
                bone->get().parent_node() : bone->get().child_node();
            auto& rotating = rotation->pivot == rotation_pivot::root ?
                bone->get().child_node() : bone->get().parent_node();
            context.rotation = rotation_evaluation_context{pivot.world_pos(), rotating.world_pos()};
            if (progress > 0.0) bone->get().rotate_by(rotation->angle * eased, pivot,
                rotation->propagation == rotation_propagation::bone_only);
            continue;
        }

        if (const auto* rotation = std::get_if<ik_rotation>(&action->data)) {
            auto effector = working.get<sm::node>(rotation->effector);
            auto pivot = working.get<sm::node>(rotation->pivot_node);
            if (!effector || !pivot || effector->get().id() == pivot->get().id() ||
                &effector->get().owner() != &pivot->get().owner() || !std::isfinite(rotation->angle)) {
                report.invalid_actions.push_back(action->id);
                continue;
            }
            const auto origin = pivot->get().world_pos();
            const auto effector_pos = effector->get().world_pos();
            context.rotation = rotation_evaluation_context{origin, effector_pos};
            const double radius = sm::distance(origin, effector_pos);
            if (!(radius > 0.0) || !std::isfinite(radius)) {
                report.invalid_actions.push_back(action->id);
                continue;
            }
            if (progress > 0.0) {
                const double theta = sm::angle_from_u_to_v(origin, effector_pos) + rotation->angle * eased;
                const sm::point target = origin + radius * sm::point(std::cos(theta), std::sin(theta));
                sm::perform_fabrik(*effector, target, *pivot);
            }
            continue;
        }

        if(const auto* translation=std::get_if<rigid_translation>(&action->data)) {
            auto frame=translation_reference_frame(translation->reference,translation->reference_bone,character_root_bone,base,working);
            context.translation_reference_frame=frame;
            if(!frame) { report.invalid_actions.push_back(action->id); continue; }
            bool valid=!translation->skeletons.empty();
            std::vector<sm::skel_ref> targets;
            for(auto id:translation->skeletons) {
                auto skeleton=working.skeleton(id);
                if(!skeleton) { valid=false; break; }
                targets.push_back(*skeleton);
            }
            if(!valid) { report.invalid_actions.push_back(action->id); continue; }
            if (progress > 0.0) {
                const auto local=translation->path.evaluate_by_arc_length(progress);
                const auto world_delta=frame->vector_to_world(local);
                auto matrix=sm::translation_matrix(world_delta);
                for(auto skeleton:targets) skeleton->apply(matrix);
            }
            continue;
        }

        if(const auto* translation=std::get_if<ik_translation>(&action->data)) {
            auto effector=working.get<sm::node>(translation->effector);
            auto frame=translation_reference_frame(translation->reference,translation->reference_bone,character_root_bone,base,working);
            context.translation_reference_frame=frame;
            if (effector) context.translation_anchor_world=effector->get().world_pos();
            if(!effector || !frame) { report.invalid_actions.push_back(action->id); continue; }
            std::vector<sm::node_ref> pins; bool valid=true;
            pins.reserve(translation->pins.size());
            for(auto id:translation->pins) {
                auto pin=working.get<sm::node>(id);
                if(!pin || id==translation->effector || &pin->get().owner()!=&effector->get().owner()) { valid=false; break; }
                pins.push_back(*pin);
            }
            if(!valid) { report.invalid_actions.push_back(action->id); continue; }
            if (progress > 0.0) {
                const auto displacement=translation->path.evaluate_by_arc_length(progress);
                // IK translation composes with preceding actions: its path displaces
                // the effector from the pose handed to this action, not from an authored snapshot.
                const auto target=effector->get().world_pos()+frame->vector_to_world(displacement);
                sm::perform_fabrik(std::vector<std::tuple<sm::node_ref,sm::point>>{{*effector,target}},pins);
            }
            continue;
        }

        report.unsupported_actions.push_back(action->id);
    }
    return report;
}
