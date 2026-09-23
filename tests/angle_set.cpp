#include "core/sm_angle_set.hpp"
#include <cmath>
#include <iostream>
#include <limits>
#include <numbers>
#include <stdexcept>

namespace {
constexpr double pi = std::numbers::pi;
constexpr double tau = 2 * pi;
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
void near(std::optional<double> actual, double expected, const char* message) {
    require(actual && std::abs(*actual - expected) < 1e-12, message);
}
template<class F> void rejects(F operation) {
    try { operation(); } catch (const std::invalid_argument&) { return; }
    throw std::runtime_error("invalid input accepted");
}
void full_and_empty() {
    sm::angle_set full;
    require(full.contains(-123.0) && full.contains(987.0), "default is not full circle");
    near(full.closest_angle(-pi / 2), 3 * pi / 2, "closest angle not normalized");
    require(sm::angle_set({1.0, tau}).contains(0), "explicit full turn lost seam");
    auto empty = sm::angle_set::empty();
    require(empty.is_empty() && !empty.contains(0) && !empty.closest_angle(0), "empty set has a member");
    require(full.intersect(empty).is_empty(), "empty intersection not empty");
    require(empty.shifted(1).negated().is_empty(), "transform populated empty set");
}
void wrap_and_seam() {
    sm::angle_set wrap({3 * pi / 2, pi});
    require(wrap.contains(0) && wrap.contains(-pi / 2) && wrap.contains(pi / 2), "wrapped arc lost boundary");
    require(!wrap.contains(pi), "wrapped arc filled excluded gap");
    auto seam = sm::angle_set({pi, pi}).intersect(sm::angle_set({0, pi / 2}));
    require(!seam.is_empty() && seam.contains(0) && seam.contains(tau), "seam singleton lost");
    require(!seam.contains(0.01), "seam singleton widened");
    near(seam.closest_angle(1), 0, "seam closest angle incorrect");
    require(sm::angle_set({tau, 0}).contains(0), "zero-span seam lost");
    const auto singleton = sm::angle_set({1, 0});
    const double adjacent = std::nextafter(1.0, 2.0);
    require(!singleton.contains(adjacent), "singleton accepts another representable angle");
    require(singleton.closest_angle(adjacent) == 1.0, "closest angle lies outside singleton");
}
void disconnected_intersection() {
    auto both = sm::angle_set({0, 3 * pi / 2}).intersect(sm::angle_set({pi, 3 * pi / 2}));
    require(both.contains(pi / 4) && both.contains(5 * pi / 4), "intersection lost a component");
    require(!both.contains(3 * pi / 4) && !both.contains(7 * pi / 4), "intersection filled a gap");
    require(both.intersect(sm::angle_set({0.6 * pi, 0.2 * pi})).is_empty(), "disjoint intersection not empty");
    near(both.closest_angle(3 * pi / 4), pi / 2, "closest tie is not deterministic");
    near(both.closest_angle(7 * pi / 4), 0, "closest ignores circular distance");
}
void transforms() {
    auto arc = sm::angle_set({pi / 4, pi / 2});
    auto shifted = arc.shifted(3 * pi / 2);
    require(shifted.contains(0) && shifted.contains(7 * pi / 4) && shifted.contains(pi / 4), "shift failed across seam");
    require(!shifted.contains(pi), "shift changed span");
    auto reflected = arc.negated();
    require(reflected.contains(5 * pi / 4) && reflected.contains(7 * pi / 4), "reflection lost endpoints");
    require(!reflected.contains(pi / 2), "reflection used wrong direction");
    auto both = sm::angle_set({0, 3 * pi / 2}).intersect(sm::angle_set({pi, 3 * pi / 2}));
    auto moved = both.shifted(pi / 4).negated();
    require(moved.contains(pi / 2) && moved.contains(3 * pi / 2), "transform lost disconnected component");
    require(!moved.contains(pi) && !moved.contains(0), "transform filled disconnected gaps");
    require(sm::angle_set().negated().shifted(1).contains(2), "transform shrank full circle");
}
void invalid_inputs() {
    const auto inf = std::numeric_limits<double>::infinity();
    const auto nan = std::numeric_limits<double>::quiet_NaN();
    for (auto range : {sm::angle_range{0, -0.1}, {0, tau + 0.1}, {inf, 1}, {0, inf}, {nan, 0}, {0, nan}})
        rejects([&] { sm::angle_set set(range); });
    rejects([&] { sm::angle_set().shifted(inf); });
    require(!sm::angle_set().contains(nan) && !sm::angle_set().closest_angle(inf), "nonfinite query accepted");
}
}
int main() {
    try {
        full_and_empty(); wrap_and_seam(); disconnected_intersection(); transforms(); invalid_inputs();
        std::cout << "angle_set tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
