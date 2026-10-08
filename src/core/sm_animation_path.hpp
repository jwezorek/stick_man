#pragma once

#include "sm_types.hpp"
#include "sm_object_id.hpp"
#include <vector>

namespace sm {
// Coordinates are in the fixed animation-start root frame. Endpoints are supplied
// by poses, never persisted with this object. Handles are offsets from knots.
enum class path_shape { line, cubic, spline };
struct path_knot {
    point position{};       // only authoritative for interior knots
    point handle_in{};      // offset from this knot
    point handle_out{};     // offset from this knot
};

struct animation_path {
    object_id node;
    path_shape shape = path_shape::line;
    std::vector<path_knot> knots; // includes two endpoint *handles*, not endpoint positions

    // A cached arc-length table is derived data only, and never serialized.
    point at_parameter(double parameter, point start, point end) const;
    point evaluate(double arc_fraction, point start, point end) const;
    void reset_shape(path_shape new_shape, point start, point end);
    void insert_knot(std::size_t segment, point start, point end);
    void remove_knot(std::size_t index);
    void invalidate() const;

private:
    mutable std::vector<point> cache_signature_;
    mutable std::vector<double> cumulative_lengths_;
};

// The character root bone at the *first animation key*, not at the current
// playback pose. These transforms are shared by Core evaluation and Qt drawing.
struct animation_root_frame {
    point origin{};
    double radians = 0.0;
    point to_world(point local) const;
    point to_local(point world) const;
};
}
