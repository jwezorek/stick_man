#include "core/sm_animation.hpp"
#include "core/sm_skeleton.hpp"
#include "core/sm_bone.hpp"

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

bool near_angle(double a, double b, double eps = 1e-6) {
    return std::abs(sm::angular_distance(a, b)) < eps;
}

struct fixture {
    sm::topology topology;
    sm::object_id skeleton_id;
    sm::object_id root_bone_id;
    sm::pose base;

    fixture() {
        auto& root = topology.create_skeleton(sm::point{0.0, 0.0});
        auto& tip = topology.create_skeleton(sm::point{10.0, 0.0});
        auto bone = topology.create_bone("root", root.root_node(), tip.root_node());
        require(bone.has_value(), "failed to create rotation fixture bone");
        skeleton_id = bone->get().owner().id();
        root_bone_id = bone->get().id();
        base = sm::capture_pose(topology, {skeleton_id}, "base");
    }
};

sm::animation make_rotation(const fixture& f, double angle, sm::easing easing = sm::easing::linear) {
    sm::animation animation;
    animation.name = "rotation";
    animation.base_pose = f.base.id;

    sm::animation_action action;
    action.start = 100;
    action.duration = 1000;
    action.easing = easing;

    sm::rigid_rotation rotation;
    rotation.bone = f.root_bone_id;
    rotation.pivot = sm::rotation_pivot::root;
    rotation.angle = angle;
    rotation.propagation = sm::rotation_propagation::hierarchy;
    action.data = rotation;

    animation.layers.push_back({{action}});
    return animation;
}

void rigid_rotation_is_absolute_time_evaluated() {
    fixture f;
    const double half_pi = std::acos(-1.0) / 2.0;
    const auto animation = make_rotation(f, half_pi);

    sm::evaluate_animation(animation, f.base, f.root_bone_id, f.topology, 0);
    require(near_angle(f.topology.get<sm::bone>(f.root_bone_id)->get().world_rotation(), 0.0),
        "rotation changed before action start");

    sm::evaluate_animation(animation, f.base, f.root_bone_id, f.topology, 100);
    require(near_angle(f.topology.get<sm::bone>(f.root_bone_id)->get().world_rotation(), 0.0),
        "rotation changed at action start");

    sm::evaluate_animation(animation, f.base, f.root_bone_id, f.topology, 600);
    require(near_angle(f.topology.get<sm::bone>(f.root_bone_id)->get().world_rotation(), half_pi * 0.5),
        "rotation did not evaluate to half angle at half time");

    sm::evaluate_animation(animation, f.base, f.root_bone_id, f.topology, 1100);
    require(near_angle(f.topology.get<sm::bone>(f.root_bone_id)->get().world_rotation(), half_pi),
        "rotation did not reach final angle");

    // Direct evaluation must reset to the base pose rather than integrate from the previous frame.
    sm::evaluate_animation(animation, f.base, f.root_bone_id, f.topology, 350);
    require(near_angle(f.topology.get<sm::bone>(f.root_bone_id)->get().world_rotation(), half_pi * 0.25),
        "rotation evaluation integrated from previous frame");
}

void rigid_rotation_honors_easing() {
    fixture f;
    const double half_pi = std::acos(-1.0) / 2.0;
    const auto animation = make_rotation(f, half_pi, sm::easing::ease_in);

    sm::evaluate_animation(animation, f.base, f.root_bone_id, f.topology, 600);
    const double expected = half_pi * sm::ease(sm::easing::ease_in, 0.5);
    require(near_angle(f.topology.get<sm::bone>(f.root_bone_id)->get().world_rotation(), expected),
        "rotation easing was not applied");
}

} // namespace

int main() {
    try {
        rigid_rotation_is_absolute_time_evaluated();
        rigid_rotation_honors_easing();
        std::cout << "PASS animation_rotation\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
