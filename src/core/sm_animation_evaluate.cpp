#include "sm_animation.hpp"
#include "sm_skeleton.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

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
            const auto* rotation = std::get_if<rigid_rotation>(&action->data);
            if (!rotation) { report.unsupported_actions.push_back(action->id); continue; }
            auto bone = working.get<sm::bone>(rotation->bone);
            if (!bone || !std::isfinite(rotation->angle) ||
                (rotation->pivot != rotation_pivot::root && rotation->pivot != rotation_pivot::tip)) {
                report.invalid_actions.push_back(action->id); continue;
            }
            if (time <= action->start) continue;
            const double progress = time >= action->start + action->duration ? 1.0
                : double(time - action->start) / double(action->duration);
            auto& pivot = rotation->pivot == rotation_pivot::root ? bone->get().parent_node() : bone->get().child_node();
            bone->get().rotate_by(rotation->angle * progress, pivot, false);
        }
    }
    return report;
}
