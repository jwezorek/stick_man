#include "core/sm_animation_path.hpp"

#include <cmath>
#include <iostream>
#include <numbers>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void near(sm::point actual, sm::point expected, double tolerance, const char* message) {
    require(std::isfinite(actual.x) && std::isfinite(actual.y) &&
            sm::distance(actual, expected) <= tolerance, message);
}

double magnitude(sm::point p) {
    return std::hypot(p.x, p.y);
}

void single_segment_path() {
    const sm::point start{10, 20}, end{130, 50};
    sm::animation_path path;
    path.reset(start, end);
    require(path.knots.size() == 2, "reset should make exactly one Bezier segment");
    require(path.is_smooth(), "single-segment path should be smooth");
    near(path.at_parameter(0, start, end), start, 1e-10, "start endpoint");
    near(path.at_parameter(1, start, end), end, 1e-10, "end endpoint");
    near(path.at_parameter(0.5, start, end), (start + end) * 0.5, 1e-10,
         "default cubic handles should produce a straight segment");
    near(path.evaluate(0.25, start, end), start + 0.25 * (end - start), 1e-8,
         "arc-length evaluation of a straight cubic");

    // The editor creates a curved one-segment spline by dragging its handles.
    path.set_handle(0, false, {40, 65});
    path.set_handle(1, true, {-40, 65});
    require(path.knots.size() == 2, "bending the initial curve must not add knots");
    require(path.is_smooth(), "bending the initial curve must stay valid");
    const auto halfway = path.at_parameter(0.5, start, end);
    require(halfway.y > 50, "dragged control points must bend the curve");
    near(path.evaluate(0, start, end), start, 1e-10, "evaluate at start");
    near(path.evaluate(1, start, end), end, 1e-10, "evaluate at end");
}

void inserting_knot_preserves_curve() {
    const sm::point start{-10, 15}, end{110, 45};
    sm::animation_path path;
    path.reset(start, end);
    path.set_handle(0, false, {45, 70});
    path.set_handle(1, true, {-25, -60});
    const auto before = path;

    constexpr double split = 0.317;
    const auto expected_knot = before.at_parameter(split, start, end);
    path.insert_knot(0, split, start, end);
    require(path.knots.size() == 3, "insert must create an interior knot");
    require(path.is_smooth(), "de Casteljau subdivision must produce a smooth knot");
    near(path.knots[1].position, expected_knot, 1e-9,
         "inserted knot should be at the requested curve position");

    // The new curve has two segments, each occupying half the global parameter range.
    for (int i = 0; i <= 100; ++i) {
        const double t = i / 100.0;
        const double new_parameter = t <= split
            ? 0.5 * t / split
            : 0.5 + 0.5 * (t - split) / (1 - split);
        near(path.at_parameter(new_parameter, start, end),
             before.at_parameter(t, start, end), 1e-8,
             "knot insertion must not alter curve geometry");
    }
    near(path.evaluate(0.37, start, end), before.evaluate(0.37, start, end), 0.2,
         "arc-length playback should remain essentially unchanged by subdivision");
}

void interior_handles_remain_g1_smooth() {
    const sm::point start{0, 0}, end{120, 30};
    sm::animation_path path;
    path.reset(start, end);
    path.set_handle(0, false, {30, 50});
    path.set_handle(1, true, {-40, 50});
    path.insert_knot(0, 0.41, start, end);

    const double old_in_length = magnitude(path.knots[1].handle_in);
    path.set_handle(1, false, {21, -13});
    require(path.is_smooth(), "moving outgoing handle must preserve G1 continuity");
    require(std::abs(magnitude(path.knots[1].handle_in) - old_in_length) < 1e-8,
            "outgoing handle drag must preserve incoming handle length");

    const double old_out_length = magnitude(path.knots[1].handle_out);
    path.set_handle(1, true, {-12, -19});
    require(path.is_smooth(), "moving incoming handle must preserve G1 continuity");
    require(std::abs(magnitude(path.knots[1].handle_out) - old_out_length) < 1e-8,
            "incoming handle drag must preserve outgoing handle length");

    path.set_handle(1, true, {0, 0});
    require(path.is_smooth(), "dragging a handle to its knot must not create a cusp");
    path.insert_knot(1, 0.69, start, end);
    require(path.knots.size() == 4 && path.is_smooth(),
            "inserting into an existing spline must keep all knots smooth");
    path.remove_knot(1);
    require(path.knots.size() == 3 && path.is_smooth(),
            "removing an interior knot must leave a valid smooth spline");
}

void legacy_corner_normalization() {
    const sm::point start{0, 0}, end{80, 10};
    sm::animation_path path;
    path.reset(start, end);
    path.insert_knot(0, 0.5, start, end);
    path.knots[1].handle_in = {-13, -17};
    path.knots[1].handle_out = {14, -8}; // deliberately not collinear
    require(!path.is_smooth(), "test fixture must initially contain a corner");
    const double previous_out_length = magnitude(path.knots[1].handle_out);
    path.smooth_interior_knots();
    require(path.is_smooth(), "legacy corners should be normalized to G1 knots");
    require(std::abs(magnitude(path.knots[1].handle_out) - previous_out_length) < 1e-8,
            "normalization should preserve outgoing handle length");
}

void reject_invalid_knot_edits() {
    const sm::point start{0, 0}, end{100, 0};
    sm::animation_path path;
    path.reset(start, end);
    auto rejected = [&path](std::size_t i) {
        try { path.remove_knot(i); }
        catch (const std::out_of_range&) { return true; }
        return false;
    };
    require(rejected(0) && rejected(1), "endpoint knots must not be removable");
    try {
        path.insert_knot(0, 0, start, end);
        throw std::runtime_error("zero split parameter must be rejected");
    } catch (const std::out_of_range&) {}
    try {
        path.insert_knot(1, 0.5, start, end);
        throw std::runtime_error("invalid segment index must be rejected");
    } catch (const std::out_of_range&) {}
}

void animation_root_frame_round_trip() {
    sm::animation_root_frame frame;
    frame.origin = {31, -12};
    frame.radians = std::numbers::pi / 3;
    const sm::point local{5, 8};
    near(frame.to_local(frame.to_world(local)), local, 1e-10,
         "animation root frame world/local round trip");
    near(frame.to_world({0, 0}), frame.origin, 1e-10,
         "animation root frame origin");
}

} // namespace

int main() {
    try {
        single_segment_path();
        inserting_knot_preserves_curve();
        interior_handles_remain_g1_smooth();
        legacy_corner_normalization();
        reject_invalid_knot_edits();
        animation_root_frame_round_trip();
        std::cout << "animation_paths: passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "animation_paths: FAILED: " << error.what() << '\n';
        return 1;
    }
}
