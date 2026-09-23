#pragma once
#include "sm_types.hpp"

namespace sm {

// A finite union of closed circular arcs. Angles are measured in radians.
class angle_set {
public:
    angle_set();
    explicit angle_set(angle_range range);
    static angle_set empty();

    bool is_empty() const;
    bool contains(double angle) const;
    angle_set intersect(const angle_set& other) const;
    angle_set shifted(double offset) const;
    angle_set negated() const;

    // Returns [0, 2*pi); equidistant endpoints choose the smaller angle.
    // Empty sets and nonfinite queries have no closest angle.
    std::optional<double> closest_angle(double angle) const;

private:
    struct interval { double low, high; };
    std::vector<interval> intervals_;
    void append_arc(double start, double span);
    void canonicalize();
};

}
