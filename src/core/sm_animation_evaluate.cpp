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
}

sm::animation_evaluation sm::evaluate_animation(const animation& animation, const pose& base,
        topology& working, animation_time time) {
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

            report.unsupported_actions.push_back(action->id);
        }
    }
    return report;
}
