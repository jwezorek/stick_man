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
    struct frame { sm::point origin{}; double angle=0.0; };
    sm::point rotate(sm::point p,double angle) {
        const double c=std::cos(angle),s=std::sin(angle);
        return {c*p.x-s*p.y,s*p.x+c*p.y};
    }
    std::optional<frame> current_bone_frame(sm::object_id bone_id,sm::topology& working) {
        auto bone=working.get<sm::bone>(bone_id); if(!bone) return {};
        const auto origin=bone->get().parent_node().world_pos();
        const auto tip=bone->get().child_node().world_pos();
        return frame{origin,sm::angle_from_u_to_v(origin,tip)};
    }
    std::optional<frame> base_bone_frame(sm::object_id bone_id,const sm::pose& base,sm::topology& working) {
        auto bone=working.get<sm::bone>(bone_id); if(!bone) return {};
        const auto u=base.node_positions.find(bone->get().parent_node().id());
        const auto v=base.node_positions.find(bone->get().child_node().id());
        if(u==base.node_positions.end() || v==base.node_positions.end()) return {};
        return frame{u->second,sm::angle_from_u_to_v(u->second,v->second)};
    }
    std::optional<frame> reference_frame(sm::translation_reference reference,sm::object_id reference_bone,
            sm::object_id character_root_bone,const sm::pose& base,sm::topology& working) {
        if(!valid_reference(reference)) return {};
        if(reference==sm::translation_reference::animation_root)
            return base_bone_frame(character_root_bone,base,working);
        if(reference==sm::translation_reference::character_root)
            return current_bone_frame(character_root_bone,working);
        return current_bone_frame(reference_bone,working);
    }
}

sm::animation_evaluation sm::evaluate_animation(const animation& animation, const pose& base,
        object_id character_root_bone, topology& working, animation_time time) {
    animation.duration(); // Validate intervals before touching the working pose.
    for (auto s : working.skeletons()) for (auto n : s->nodes())
        if (!base.node_positions.contains(n->id())) throw std::invalid_argument("Incomplete animation base pose");
    apply_pose(base, working);
    animation_evaluation report;
    for (const auto& layer : animation.layers) {
        std::vector<const animation_action*> actions;
        for (const auto& action : layer.actions) actions.push_back(&action);
        std::ranges::sort(actions, {}, [](auto action) { return action->start; });
        for (const auto* action : actions) {
            if (time <= action->start) continue;
            const double progress = time >= action->start + action->duration ? 1.0
                : double(time - action->start) / double(action->duration);
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
                bone->get().rotate_by(rotation->angle * eased, pivot,
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
                const double radius = sm::distance(origin, effector_pos);
                if (!(radius > 0.0) || !std::isfinite(radius)) {
                    report.invalid_actions.push_back(action->id);
                    continue;
                }
                const double theta = sm::angle_from_u_to_v(origin, effector_pos) + rotation->angle * eased;
                const sm::point target = origin + radius * sm::point(std::cos(theta), std::sin(theta));
                sm::perform_fabrik(*effector, target, *pivot);
                continue;
            }

            if(const auto* translation=std::get_if<rigid_translation>(&action->data)) {
                auto frame=reference_frame(translation->reference,translation->reference_bone,character_root_bone,base,working);
                if(!frame) { report.invalid_actions.push_back(action->id); continue; }
                const auto local=translation->path.evaluate_by_arc_length(progress);
                const auto world_delta=rotate(local,frame->angle);
                bool valid=!translation->skeletons.empty();
                std::vector<sm::skel_ref> targets;
                for(auto id:translation->skeletons) {
                    auto skeleton=working.skeleton(id);
                    if(!skeleton) { valid=false; break; }
                    targets.push_back(*skeleton);
                }
                if(!valid) { report.invalid_actions.push_back(action->id); continue; }
                auto matrix=sm::translation_matrix(world_delta);
                for(auto skeleton:targets) skeleton->apply(matrix);
                continue;
            }

            if(const auto* translation=std::get_if<ik_translation>(&action->data)) {
                auto effector=working.get<sm::node>(translation->effector);
                auto frame=reference_frame(translation->reference,translation->reference_bone,character_root_bone,base,working);
                if(!effector || !frame) { report.invalid_actions.push_back(action->id); continue; }
                std::vector<sm::node_ref> pins; bool valid=true;
                pins.reserve(translation->pins.size());
                for(auto id:translation->pins) {
                    auto pin=working.get<sm::node>(id);
                    if(!pin || id==translation->effector || &pin->get().owner()!=&effector->get().owner()) { valid=false; break; }
                    pins.push_back(*pin);
                }
                if(!valid) { report.invalid_actions.push_back(action->id); continue; }
                const auto displacement=translation->path.evaluate_by_arc_length(progress);
                const auto target=frame->origin+rotate(translation->effector_start+displacement,frame->angle);
                sm::perform_fabrik(std::vector<std::tuple<sm::node_ref,sm::point>>{{*effector,target}},pins);
                continue;
            }

            report.unsupported_actions.push_back(action->id);
        }
    }
    return report;
}
