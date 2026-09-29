#include "core/sm_animation.hpp"
#include "json.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <numbers>
#include <stdexcept>

namespace {
constexpr double pi = std::numbers::pi;
constexpr double inf = std::numeric_limits<double>::infinity();
constexpr double nan = std::numeric_limits<double>::quiet_NaN();

void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
void near(double actual, double expected) {
    require(std::abs(actual - expected) < 1e-12, "incorrect interpolated scalar");
}
sm::object_id id(unsigned char value) {
    sm::object_id::storage_type bytes{};
    bytes.back() = value;
    return sm::object_id(bytes);
}
void same_pose(const sm::skeletal_pose& a, const sm::skeletal_pose& b) {
    require(a.root_positions == b.root_positions && a.bone_rotations == b.bone_rotations,
        "stored pose scalars changed");
}
void same_sample(const sm::reference_pose_sample& a, const sm::reference_pose_sample& b) {
    same_pose(a.pose, b.pose);
    require(a.location.index() == b.location.index(), "sample kind changed");
    if (const auto* key = std::get_if<sm::reference_keyframe>(&a.location)) {
        require(key->keyframe_id == std::get<sm::reference_keyframe>(b.location).keyframe_id,
            "keyframe identity changed");
    } else {
        const auto& x = std::get<sm::reference_transition>(a.location);
        const auto& y = std::get<sm::reference_transition>(b.location);
        require(x.transition_id == y.transition_id && x.from_keyframe_id == y.from_keyframe_id &&
            x.to_keyframe_id == y.to_keyframe_id && x.progress == y.progress,
            "transition metadata changed");
    }
}
sm::animation sequence() {
    sm::animation a;
    a.name = "Reference fixture";
    a.keyframes.resize(3);
    a.keyframes[0].name = "Start";
    a.keyframes[1].name = "Middle";
    a.reconcile_transitions();
    a.transitions[0].duration_seconds = 0.4;
    a.transitions[1].duration_seconds = 1.2;
    a.keyframes[0].pose = {{{id(1), {0, 8}}, {id(2), {-8, 4}}},
        {{id(3), 0}, {id(4), 1}, {id(5), -1}}};
    a.keyframes[1].pose = {{{id(1), {4, 0}}, {id(2), {8, -4}}},
        {{id(3), 1}, {id(4), -1}, {id(5), 0}}};
    a.keyframes[2].pose = {{{id(1), {16, 12}}, {id(2), {4, 8}}},
        {{id(3), 2}, {id(4), 0}, {id(5), 1}}};
    return a;
}
sm::reference_pose_sample sample(const sm::animation& a, double t) {
    auto result = sm::sample_reference_pose(a, t);
    require(result.has_value(), "missing sample");
    return *result;
}
void exact(const sm::animation& a, double t, std::size_t index) {
    const auto result = sample(a, t);
    const auto* key = std::get_if<sm::reference_keyframe>(&result.location);
    require(key && key->keyframe_id == a.keyframes[index].id, "wrong exact keyframe");
    same_pose(result.pose, a.keyframes[index].pose);
}
void transition(const sm::animation& a, const sm::reference_pose_sample& result,
        std::size_t index, double progress) {
    const auto* location = std::get_if<sm::reference_transition>(&result.location);
    require(location && location->transition_id == a.transitions[index].id &&
        location->from_keyframe_id == a.keyframes[index].id &&
        location->to_keyframe_id == a.keyframes[index + 1].id, "wrong transition endpoints");
    near(location->progress, progress);
}
template<class F> void invalid(F&& fn) {
    bool rejected = false;
    try { fn(); } catch (const std::invalid_argument&) { rejected = true; }
    require(rejected, "bad sampling input was not explicitly rejected");
}

void empty_single_clamping_and_invalid_time() {
    sm::animation empty;
    for (double t : {-100.0, 0.0, 100.0}) {
        require(!sm::sample_reference_pose(empty, t), "empty sequence returned a pose");
    }
    auto one = sequence();
    one.keyframes.resize(1);
    one.reconcile_transitions();
    one.keyframes[0].pose.bone_rotations.at(id(3)) = 7 * pi;
    for (double t : { -1e300, 0.0, 0.2, 1e300 })
        exact(one, t, 0);
    auto a = sequence();
    exact(a, -10, 0);
    exact(a, 10, 2);
    for (const auto* source : {&empty, &one, &a}) {
        for (double t : {nan, -inf, inf}) {
            invalid([&] { sm::sample_reference_pose(*source, t); });
        }
    }
}

void three_pose_timing_and_stable_ids() {
    const auto a = sequence();
    exact(a, 0, 0);
    exact(a, a.transitions[0].duration_seconds, 1);
    exact(a, a.duration_seconds(), 2);
    const auto first = sample(a, 0.2);
    transition(a, first, 0, 0.5);
    near(first.pose.root_positions.at(id(1)).x, 2);
    near(first.pose.root_positions.at(id(1)).y, 4);
    near(first.pose.root_positions.at(id(2)).x, 0);
    near(first.pose.root_positions.at(id(2)).y, 0);
    near(first.pose.bone_rotations.at(id(3)), 0.5);
    near(first.pose.bone_rotations.at(id(4)), 0);
    near(first.pose.bone_rotations.at(id(5)), -0.5);
    const auto second = sample(a, 1.0);
    transition(a, second, 1, 0.5);
    near(second.pose.root_positions.at(id(1)).x, 10);
    near(second.pose.root_positions.at(id(1)).y, 6);
    near(second.pose.root_positions.at(id(2)).x, 6);
    near(second.pose.root_positions.at(id(2)).y, 2);
    near(second.pose.bone_rotations.at(id(3)), 1.5);
    near(second.pose.bone_rotations.at(id(4)), -0.5);
    near(second.pose.bone_rotations.at(id(5)), 0.5);
    const auto quarter = sample(a, 0.1);
    transition(a, quarter, 0, 0.25);
    near(quarter.pose.root_positions.at(id(1)).x, 1);
    near(quarter.pose.bone_rotations.at(id(3)), 0.25);
}

void exact_endpoints_and_adjacent_times() {
    auto a = sequence();
    a.keyframes[1].pose.bone_rotations.at(id(3)) = 8 * pi + 1;
    const double boundary = a.transitions[0].duration_seconds;
    exact(a, boundary, 1);
    const double before = std::nextafter(boundary, -inf);
    const double after = std::nextafter(boundary, inf);
    auto left = sample(a, before);
    auto right = sample(a, after);
    transition(a, left, 0, before / 0.4);
    transition(a, right, 1, (after - boundary) / 1.2);
    require(std::get<sm::reference_transition>(left.location).progress < 1 &&
        std::get<sm::reference_transition>(right.location).progress > 0,
        "near-boundary samples snapped to an endpoint");
    near(left.pose.bone_rotations.at(id(3)), 1);
    near(right.pose.bone_rotations.at(id(3)), 1);
    near(left.pose.root_positions.at(id(1)).x, 4);
    near(right.pose.root_positions.at(id(1)).x, 4);
    require(std::holds_alternative<sm::reference_transition>(
        sample(a, std::nextafter(0.0, inf)).location), "beginning epsilon consumed time");
    require(std::holds_alternative<sm::reference_transition>(
        sample(a, std::nextafter(a.duration_seconds(), -inf)).location),
        "ending epsilon consumed time");
}

void shortest_angles_and_half_turns() {
    auto a = sequence();
    a.keyframes.resize(2);
    a.reconcile_transitions();
    struct angle_case { double from, to, quarter; };
    for (const auto& c : {angle_case{3*pi/4, -3*pi/4, 7*pi/8},
            {-3*pi/4, 3*pi/4, -7*pi/8}, {0, 2*pi, 0}, {2*pi, 0, 0},
            {pi, -pi, pi}, {0, pi, pi/4}, {pi, 0, 3*pi/4},
            {0, -pi, -pi/4}, {-pi, 0, -3*pi/4}}) {
        a.keyframes[0].pose.bone_rotations.at(id(3)) = c.from;
        a.keyframes[1].pose.bone_rotations.at(id(3)) = c.to;
        near(sample(a, 0.1).pose.bone_rotations.at(id(3)), c.quarter);
        exact(a, 0, 0);
        exact(a, a.duration_seconds(), 1);
    }
}

template<class Map> void reverse_insertion(Map& map) {
    std::vector<typename Map::value_type> entries(map.begin(), map.end());
    map.clear();
    map.rehash(67);
    for (auto it = entries.rbegin(); it != entries.rend(); ++it)
        map.insert(*it);
}

void history_order_immutability_and_roundtrip() {
    sm::animation_assets assets;
    assets.poses.emplace_back();
    assets.poses.back().name = "Default";
    assets.default_pose = assets.poses.back().id;
    assets.animations.push_back(sequence());
    const auto before = sm::animation_assets_to_json(assets).dump();
    const auto& a = assets.animations.front();
    const auto t1 = sample(a, 0.1);
    sample(a, 1.3);
    same_sample(t1, sample(a, 0.1));
    auto reordered = a;
    for (auto& key : reordered.keyframes) {
        reverse_insertion(key.pose.root_positions);
        reverse_insertion(key.pose.bone_rotations);
    }
    const auto loaded = sm::animation_assets_from_json(nlohmann::json::parse(before));
    for (double t : {-5.0, 0.0, 0.1, 0.2, a.transitions[0].duration_seconds,
            std::nextafter(0.4, -inf), std::nextafter(0.4, inf), 1.0, a.duration_seconds(), 10.0}) {
        same_sample(sample(a, t), sample(reordered, t));
        same_sample(sample(a, t), sample(loaded.animations.front(), t));
    }
    require(sm::animation_assets_to_json(assets).dump() == before,
        "sampling mutated source IDs, names, durations, poses or serialized assets");
}

void malformed_sequences_fail_before_any_sample() {
    auto reject = [](const sm::animation& a) {
        for (double t : {-10.0, 0.0, 0.2, 0.4, 1.0, 100.0}) {
            invalid([&] { sm::sample_reference_pose(a, t); });
        }
    };
    auto a = sequence();
    a.transitions.pop_back();
    reject(a);
    a = sequence();
    a.transitions.emplace_back();
    reject(a);
    a.keyframes.clear();
    reject(a);
    for (double duration : {0.0, -1.0, nan, inf, -inf}) {
        a = sequence();
        a.transitions[1].duration_seconds = duration;
        reject(a);
    }
    a = sequence();
    for (auto& t : a.transitions)
        t.duration_seconds = std::numeric_limits<double>::max();
    reject(a);
    for (double value : {nan, inf, -inf}) {
        a = sequence();
        a.keyframes.back().pose.root_positions.at(id(1)).x = value;
        reject(a);
        a = sequence();
        a.keyframes.back().pose.root_positions.at(id(2)).y = value;
        reject(a);
        a = sequence();
        a.keyframes.back().pose.bone_rotations.at(id(3)) = value;
        reject(a);
    }
    a = sequence();
    a.keyframes.back().pose.root_positions.erase(id(1));
    reject(a);
    a.keyframes.back().pose.root_positions.emplace(id(6), sm::point{0, 0});
    reject(a); // Same count, different ID.
    a = sequence();
    a.keyframes.back().pose.bone_rotations.erase(id(3));
    reject(a);
    a.keyframes.back().pose.bone_rotations.emplace(id(6), 0);
    reject(a);
    a = sequence();
    a.keyframes.back().id = a.keyframes.front().id;
    reject(a);
    a = sequence();
    a.transitions.back().id = {};
    reject(a);
    a = sequence();
    a.keyframes.back().pose.bone_rotations.emplace(sm::object_id{}, 0);
    reject(a);
}

void extreme_finite_values_and_collapsed_timestamps() {
    auto a = sequence();
    const double largest = std::numeric_limits<double>::max();
    a.keyframes[0].pose.root_positions.at(id(1)) = {-largest, largest};
    a.keyframes[1].pose.root_positions.at(id(1)) = {largest, -largest};
    a.keyframes[0].pose.bone_rotations.at(id(3)) = -largest;
    a.keyframes[1].pose.bone_rotations.at(id(3)) = largest;
    auto middle = sample(a, 0.2);
    near(middle.pose.root_positions.at(id(1)).x, 0);
    near(middle.pose.root_positions.at(id(1)).y, 0);
    require(std::isfinite(middle.pose.bone_rotations.at(id(3))), "finite angles overflowed");
    exact(a, 0, 0);
    exact(a, 0.4, 1);
    a.transitions[0].duration_seconds = 1e20;
    a.transitions[1].duration_seconds = 1;
    exact(a, a.duration_seconds(), 2);
}
}

int main() {
    try {
        empty_single_clamping_and_invalid_time();
        three_pose_timing_and_stable_ids();
        exact_endpoints_and_adjacent_times();
        shortest_angles_and_half_turns();
        history_order_immutability_and_roundtrip();
        malformed_sequences_fail_before_any_sample();
        extreme_finite_values_and_collapsed_timestamps();
        std::cout << "PASS Animation V2 Phase 3A reference sampling\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
