#include "core/sm_project.hpp"
#include "core/sm_animation.hpp"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void path_preserves_root_and_continuity() {
    // Recorded repro: a moving root, a planted foot and a cubic path on the
    // other ankle. A copied root pin allowed 53 units of root drift; skipping
    // the pose-first candidate also caused 31-unit jumps in the free foot.
    std::ifstream input(PATH_CONTINUITY_FIXTURE, std::ios::binary);
    const std::vector<std::uint8_t> bytes{
        std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    sm::project project;
    require(project.deserialize(bytes) == sm::project_result::success,
        "Cannot load path continuity fixture");
    require(project.characters().size() == 1, "Expected one fixture character");
    for (auto character : project.characters()) {
        auto animation = character->animation_data().animations.front();
        const auto& rig = character->rig().skeleton_ids();
        sm::topology sampled_geometry;
        for (auto skeleton : project.topology().skeletons()) {
            require(skeleton->copy_to(sampled_geometry).has_value(), "Cannot copy fixture geometry");
        }
        const auto frame = sm::fixed_animation_root(animation, project.topology(), rig,
            character->character_root_bone());
        require(frame.has_value(), "Missing animation root frame");
        const auto& transition = animation.transitions.front();
        require(transition.paths.size() == 1, "Expected one fixture path");
        const auto path_id = transition.paths.begin()->first;
        const auto path = transition.paths.begin()->second;
        const auto start = sm::animation_pose_node(animation.keyframes[0].pose,
            project.topology(), rig, path_id);
        const auto end = sm::animation_pose_node(animation.keyframes[1].pose,
            project.topology(), rig, path_id);
        require(start && end, "Missing path endpoints");
        std::map<sm::object_id, sm::point> pin_positions;
        for (auto id : transition.pinned_nodes) {
            pin_positions.emplace(id, *sm::animation_pose_node(animation.keyframes[0].pose,
                project.topology(), rig, id));
        }

        for (bool with_path : {true, false}) {
            if (!with_path) {
                animation.transitions.front().paths.clear();
            }
            std::map<sm::object_id, sm::point> previous;
            std::vector<sm::skeletal_pose> poses;
            double maximum_step = 0.0;
            for (int i = 0; i <= 140; ++i) {
                const double progress = i / 140.0;
                const double time = progress * transition.duration_seconds;
                auto sample = sm::sample_constrained_pose(animation, time,
                    project.topology(), rig, character->character_root_bone());
                require(sample && *sample, "Path sample failed");
                const auto& pose = (**sample).pose;
                poses.push_back(pose);
                sm::apply_skeletal_pose(pose, sampled_geometry, rig);
                for (const auto& [id, from] : animation.keyframes[0].pose.root_positions) {
                    const auto to = animation.keyframes[1].pose.root_positions.at(id);
                    const auto expected = (1.0 - progress) * from + progress * to;
                    require(sm::distance(expected, pose.root_positions.at(id)) < 1e-6,
                        "Path solver moved the fixed animation root");
                }
                for (const auto& [id, position] : pin_positions) {
                    require(sm::distance(sampled_geometry.get<sm::node>(id)->get().world_pos(),
                        position) < 0.005, "Path solver moved the planted foot beyond IK tolerance");
                }
                if (with_path) {
                    const auto target = frame->to_world(path.evaluate(progress,
                        frame->to_local(*start), frame->to_local(*end)));
                    require(sm::distance(sampled_geometry.get<sm::node>(path_id)->get().world_pos(),
                        target) < 0.005, "Ankle did not follow its reachable path");
                }
                for (auto skeleton : sampled_geometry.skeletons()) {
                    for (auto node : skeleton->nodes()) {
                        if (previous.contains(node->id())) {
                            maximum_step = std::max(maximum_step,
                                sm::distance(previous.at(node->id()), node->world_pos()));
                        }
                        previous[node->id()] = node->world_pos();
                    }
                }
            }
            // Samples are 10 ms apart. Normal motion stays below 7 units in
            // this fixture; branch/refinement jumps exceeded 30 units.
            require(maximum_step < 10.0, "Path animation snapped between adjacent samples");
            std::cout << (with_path ? "With path" : "Without path")
                << " maximum 10 ms displacement: " << maximum_step << '\n';

            // Scrubbing must select the same pose regardless of sampling order.
            for (int i = 140; i >= 0; i -= 7) {
                auto sample = sm::sample_constrained_pose(animation,
                    (i / 140.0) * transition.duration_seconds, project.topology(), rig,
                    character->character_root_bone());
                require(sample && *sample, "Reverse seek failed");
                require((**sample).pose.root_positions == poses[i].root_positions
                    && (**sample).pose.bone_rotations == poses[i].bone_rotations,
                    "Path pose depends on sampling order");
            }
        }
    }
}

}

int main() {
    try {
        path_preserves_root_and_continuity();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
