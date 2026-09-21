#include "core/sm_fabrik.hpp"
#include "core/sm_skeleton.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
void at(sm::node_ref node, sm::point expected, const char* message) {
    require(sm::distance(node->world_pos(), expected) < 1e-8, message);
}
struct fixture {
    sm::topology topology;
    sm::node_ref add(sm::point position) { return topology.create_skeleton(position).root_node(); }
    void link(sm::node_ref a, sm::node_ref b) {
        require(topology.create_bone("bone", a, b).has_value(), "cannot create bone");
    }
};
void pin_blocks_propagation() {
    fixture f;
    auto outside = f.add({-10, 0}), pin = f.add({0, 0});
    auto elbow = f.add({10, 0}), hand = f.add({10, 10});
    auto branch = f.add({-10, 10});
    f.link(outside, pin); f.link(pin, elbow); f.link(elbow, hand); f.link(outside, branch);
    sm::perform_fabrik(hand, {12, 8}, pin);
    at(outside, {-10, 0}, "IK moved geometry beyond pin");
    at(branch, {-10, 10}, "IK moved branch beyond pin");
    at(pin, {0, 0}, "IK moved pin");
    require(sm::distance(hand->world_pos(), {12, 8}) < 0.02, "reachable target missed");
    require(std::abs(sm::distance(pin->world_pos(), elbow->world_pos()) - 10) < 0.02, "upper bone length changed");
    require(std::abs(sm::distance(elbow->world_pos(), hand->world_pos()) - 10) < 0.02, "lower bone length changed");
}
void multiple_boundaries_and_failure() {
    fixture f;
    auto left = f.add({-10, 0}), a = f.add({0, 0}), hand = f.add({10, 10});
    auto b = f.add({20, 0}), right = f.add({30, 0});
    f.link(left, a); f.link(a, hand); f.link(hand, b); f.link(b, right);
    sm::fabrik_options opts;
    opts.max_iterations = 2;
    sm::perform_fabrik({{hand, {100, 100}}}, {a, b}, opts);
    at(a, {0, 0}, "first boundary moved on failure");
    at(b, {20, 0}, "second boundary moved on failure");
    at(left, {-10, 0}, "left region moved on failure");
    at(right, {30, 0}, "right region moved on failure");
}
void separated_effectors_are_independent() {
    fixture f;
    auto pin = f.add({0, 0}), le = f.add({-10, 0}), lh = f.add({-10, 10});
    auto re = f.add({10, 0}), rh = f.add({10, 10}), unused = f.add({0, -10});
    f.link(pin, le); f.link(le, lh); f.link(pin, re); f.link(re, rh); f.link(pin, unused);
    sm::perform_fabrik(lh, {-12, 8}, pin);
    const auto left_elbow = le->world_pos(), left_hand = lh->world_pos();
    at(re, {10, 0}, "inactive effector region moved");
    sm::perform_fabrik(rh, {12, 8}, pin);
    const auto right_elbow = re->world_pos(), right_hand = rh->world_pos();
    at(le, left_elbow, "right solve disturbed left elbow");
    at(lh, left_hand, "right solve disturbed left hand");
    le->set_world_pos({-10, 0}); lh->set_world_pos({-10, 10});
    re->set_world_pos({10, 0}); rh->set_world_pos({10, 10});
    sm::perform_fabrik({{rh, {12, 8}}, {lh, {-12, 8}}}, {pin});
    at(le, left_elbow, "combined solve changed left result");
    at(lh, left_hand, "combined solve changed left target");
    at(re, right_elbow, "combined solve changed right result");
    at(rh, right_hand, "combined solve changed right target");
    at(pin, {0, 0}, "shared pin moved");
    at(unused, {0, -10}, "untargeted branch moved");
}
void no_pins_and_pinned_effector() {
    fixture f;
    auto a = f.add({0, 0}), b = f.add({10, 0});
    f.link(a, b);
    sm::perform_fabrik(b, {12, 4}, {});
    at(b, {12, 4}, "unpinned effector missed target");
    require(std::abs(sm::distance(a->world_pos(), b->world_pos()) - 10) < 1e-8, "unpinned bone length changed");
    const auto original = a->world_pos();
    require(sm::perform_fabrik(b, {100, 100}, b) == sm::result::fabrik_no_solution_found,
        "conflicting pinned effector accepted");
    at(b, {12, 4}, "pinned effector moved");
    at(a, original, "conflicting request changed geometry");
}
void reachable_target_between_two_pins() {
    fixture f;
    auto outside = f.add({-10, 0}), a = f.add({0, 0}), elbow = f.add({10, 0});
    auto hand = f.add({10, 10}), other_elbow = f.add({20, 10}), b = f.add({20, 0});
    auto far_side = f.add({30, 0});
    f.link(outside, a); f.link(a, elbow); f.link(elbow, hand);
    f.link(hand, other_elbow); f.link(other_elbow, b); f.link(b, far_side);
    sm::perform_fabrik({{hand, {10, 8}}}, {a, b});
    at(a, {0, 0}, "first pin moved in reachable solve");
    at(b, {20, 0}, "second pin moved in reachable solve");
    at(outside, {-10, 0}, "geometry before first pin moved");
    at(far_side, {30, 0}, "geometry after second pin moved");
    require(sm::distance(hand->world_pos(), {10, 8}) < 0.02, "two-pin reachable target missed");
    for (auto endpoints : std::vector<std::pair<sm::node_ref, sm::node_ref>>{
            {a, elbow}, {elbow, hand}, {hand, other_elbow}, {other_elbow, b}}) {
        require(std::abs(sm::distance(endpoints.first->world_pos(), endpoints.second->world_pos()) - 10) < 0.02,
            "two-pin solve changed bone length");
    }
}
}
int main() {
    try {
        pin_blocks_propagation();
        multiple_boundaries_and_failure();
        separated_effectors_are_independent();
        no_pins_and_pinned_effector();
        reachable_target_between_two_pins();
        std::cout << "PASS fabrik_pins\n";
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
