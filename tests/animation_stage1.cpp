#include "core/sm_animation.hpp"
#include "core/sm_skeleton.hpp"
#include "core/sm_bone.hpp"
#include "json.hpp"

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <variant>
#include <vector>

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

bool near(double a, double b, double eps = 1e-6) {
    return std::abs(a - b) < eps;
}

void motion_paths_are_persistent_displacement_paths() {
    sm::motion_path line(sm::line_path{{0.0, 0.0}, {10.0, 0.0}});
    require(line.kind() == sm::motion_path_kind::straight, "line path kind is wrong");
    const auto halfway = line.evaluate_by_arc_length(0.5);
    require(near(halfway.x, 5.0) && near(halfway.y, 0.0), "line path midpoint is wrong");

    sm::spline_path spline;
    spline.segments.push_back({{0.0, 0.0}, {3.0, 0.0}, {7.0, 10.0}, {10.0, 10.0}});
    spline.segments.push_back({{10.0, 10.0}, {13.0, 10.0}, {17.0, 0.0}, {20.0, 0.0}});
    sm::motion_path path(std::move(spline));
    require(path.kind() == sm::motion_path_kind::spline, "spline path kind is wrong");
    const auto end = path.final_displacement();
    require(near(end.x, 20.0) && near(end.y, 0.0), "spline final displacement is wrong");
}

void translation_actions_round_trip_with_bone_reference() {
    sm::animation_assets assets;

    sm::pose base;
    base.name = "Default";
    const auto pose_node = sm::object_id::generate();
    base.node_positions.emplace(pose_node, sm::point{1.0, 2.0});
    assets.default_pose = base.id;
    assets.poses.push_back(base);

    const auto skeleton_id = sm::object_id::generate();
    const auto reference_bone = sm::object_id::generate();
    const auto effector = sm::object_id::generate();
    const auto pin = sm::object_id::generate();

    sm::animation animation;
    animation.name = "translations";
    animation.base_pose = base.id;

    sm::animation_action rigid_action;
    rigid_action.start = 0;
    rigid_action.duration = 500;
    sm::rigid_translation rigid;
    rigid.skeletons = {skeleton_id};
    rigid.path = sm::motion_path(sm::line_path{{0.0, 0.0}, {12.0, 4.0}});
    rigid.reference = sm::translation_reference::bone;
    rigid.reference_bone = reference_bone;
    rigid_action.data = rigid;

    sm::animation_action ik_action;
    ik_action.start = 500;
    ik_action.duration = 500;
    sm::ik_translation ik;
    ik.effector = effector;
    ik.pins = {pin};
    sm::spline_path spline;
    spline.segments.push_back({{0.0, 0.0}, {2.0, 0.0}, {4.0, 3.0}, {6.0, 3.0}});
    ik.path = sm::motion_path(std::move(spline));
    ik.reference = sm::translation_reference::bone;
    ik.reference_bone = reference_bone;
    ik.effector_start = {5.0, -2.0};
    ik_action.data = ik;

    animation.layers.push_back({{rigid_action, ik_action}});
    assets.animations.push_back(animation);

    const auto json = sm::animation_assets_to_json(assets);
    const auto restored = sm::animation_assets_from_json(json);
    require(restored.animations.size() == 1, "animation round trip lost animation");
    require(restored.animations.front().layers.size() == 1, "animation round trip lost layer");
    require(restored.animations.front().layers.front().actions.size() == 2, "animation round trip lost actions");

    const auto& restored_rigid = std::get<sm::rigid_translation>(
        restored.animations.front().layers.front().actions[0].data);
    require(restored_rigid.reference == sm::translation_reference::bone, "rigid translation reference changed");
    require(restored_rigid.reference_bone == reference_bone, "rigid translation reference bone changed");
    require(restored_rigid.skeletons == std::vector<sm::object_id>{skeleton_id}, "rigid translation targets changed");

    const auto& restored_ik = std::get<sm::ik_translation>(
        restored.animations.front().layers.front().actions[1].data);
    require(restored_ik.reference == sm::translation_reference::bone, "IK translation reference changed");
    require(restored_ik.reference_bone == reference_bone, "IK translation reference bone changed");
    require(restored_ik.effector == effector, "IK translation effector changed");
    require(restored_ik.pins == std::vector<sm::object_id>{pin}, "IK translation pins changed");
    require(near(restored_ik.effector_start.x, 5.0) && near(restored_ik.effector_start.y, -2.0),
        "IK translation start changed");
    require(std::holds_alternative<sm::spline_path>(restored_ik.path.geometry()),
        "IK translation spline was not preserved");
}

void root_reference_frames_use_the_character_root_bone() {
    sm::topology topology;
    auto& root = topology.create_skeleton(sm::point{0.0, 0.0});
    auto& tip = topology.create_skeleton(sm::point{0.0, 10.0});
    auto created = topology.create_bone("character-root", root.root_node(), tip.root_node());
    require(created.has_value(), "failed to create root-frame fixture");

    const auto root_bone = created->get().id();
    const auto skeleton = created->get().owner().id();
    const auto base = sm::capture_pose(topology, {skeleton}, "base");

    sm::animation animation;
    animation.base_pose = base.id;
    sm::animation_action action;
    action.start = 0;
    action.duration = 1000;
    sm::rigid_translation translation;
    translation.skeletons = {skeleton};
    translation.reference = sm::translation_reference::animation_root;
    translation.path = sm::motion_path(sm::line_path{{0.0, 0.0}, {10.0, 0.0}});
    action.data = translation;
    animation.layers.push_back({{action}});

    const auto report = sm::evaluate_animation(animation, base, root_bone, topology, 1000);
    require(report.invalid_actions.empty(), "root-frame translation evaluated as invalid");
    const auto bone = topology.get<sm::bone>(root_bone);
    require(bone.has_value(), "root bone disappeared during evaluation");
    const auto position = bone->get().parent_node().world_pos();
    require(near(position.x, 0.0) && near(position.y, 10.0),
        "Animation Root did not use the character root bone's starting orientation");
}

} // namespace

int main() {
    try {
        motion_paths_are_persistent_displacement_paths();
        translation_actions_round_trip_with_bone_reference();
        root_reference_frames_use_the_character_root_bone();
        std::cout << "PASS animation_stage1\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
