#include "sm_animation_path.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace {
sm::point mix(sm::point a, sm::point b, double t) {
    return (1 - t) * a + t * b;
}

sm::point cubic(sm::point a, sm::point b, sm::point c, sm::point d, double t) {
    const auto ab = mix(a, b, t);
    const auto bc = mix(b, c, t);
    const auto cd = mix(c, d, t);
    return mix(mix(ab, bc, t), mix(bc, cd, t), t);
}

sm::point knot_position(const sm::animation_path& path, std::size_t i, sm::point start, sm::point end) {
    return i == 0 ? start : (i + 1 == path.knots.size() ? end : path.knots[i].position);
}

double length(sm::point p) {
    return std::hypot(p.x, p.y);
}

// A zero-length handle has no tangent direction. Keep editor-created interior
// handles nonzero even when dragged directly onto their knot.
constexpr double min_handle_length = 1e-6;
constexpr std::size_t samples_per_segment = 96;
}

sm::point sm::animation_root_frame::to_world(point p) const {
    const auto c = std::cos(radians);
    const auto s = std::sin(radians);
    return {origin.x + c * p.x - s * p.y, origin.y + s * p.x + c * p.y};
}

sm::point sm::animation_root_frame::to_local(point p) const {
    p = p - origin;
    const auto c = std::cos(radians);
    const auto s = std::sin(radians);
    return {c * p.x + s * p.y, -s * p.x + c * p.y};
}

void sm::animation_path::invalidate() const {
    cache_signature_.clear();
    cumulative_lengths_.clear();
}

sm::point sm::animation_path::at_parameter(double u, point start, point end) const {
    u = std::clamp(u, 0.0, 1.0);
    if (knots.size() < 2)
        return mix(start, end, u);
    const auto segments = knots.size() - 1;
    const double scaled = u * segments;
    const auto i = std::min(static_cast<std::size_t>(scaled), segments - 1);
    const auto t = i + 1 == segments && u == 1.0 ? 1.0 : scaled - i;
    const auto a = knot_position(*this, i, start, end);
    const auto b = knot_position(*this, i + 1, start, end);
    return cubic(a, a + knots[i].handle_out, b + knots[i + 1].handle_in, b, t);
}

sm::point sm::animation_path::evaluate(double fraction, point start, point end) const {
    if (!std::isfinite(fraction))
        fraction = 0;
    fraction = std::clamp(fraction, 0.0, 1.0);
    if (fraction == 0)
        return start;
    if (fraction == 1)
        return end;
    if (knots.size() < 2)
        return mix(start, end, fraction);
    std::vector<point> signature{start, end};
    for (const auto& k : knots) {
        signature.push_back(k.position);
        signature.push_back(k.handle_in);
        signature.push_back(k.handle_out);
    }
    if (signature != cache_signature_ || cumulative_lengths_.empty()) {
        cache_signature_ = std::move(signature);
        const auto steps = (knots.size() - 1) * samples_per_segment;
        cumulative_lengths_.clear();
        cumulative_lengths_.reserve(steps + 1);
        cumulative_lengths_.push_back(0);
        auto prev = start;
        for (std::size_t i = 1; i <= steps; ++i) {
            const auto next = at_parameter(double(i) / steps, start, end);
            cumulative_lengths_.push_back(cumulative_lengths_.back() + distance(prev, next));
            prev = next;
        }
    }
    const auto total = cumulative_lengths_.back();
    if (!(total > 1e-12) || !std::isfinite(total))
        return start;
    const auto desired = fraction * total;
    auto it = std::lower_bound(cumulative_lengths_.begin(), cumulative_lengths_.end(), desired);
    const auto idx = static_cast<std::size_t>(it - cumulative_lengths_.begin());
    if (idx == 0)
        return start;
    const auto previous = cumulative_lengths_[idx - 1];
    const auto span = *it - previous;
    const auto alpha = span > 1e-15 ? (desired - previous) / span : 0.0;
    return at_parameter((idx - 1 + alpha) / (cumulative_lengths_.size() - 1), start, end);
}

void sm::animation_path::reset(point start, point end) {
    knots.resize(2);
    knots.front() = {};
    knots.back() = {};
    const auto d = end - start;
    knots.front().handle_out = (1.0 / 3) * d;
    knots.back().handle_in = (-1.0 / 3) * d;
    invalidate();
}

void sm::animation_path::set_handle(std::size_t i, bool incoming, point offset) {
    if (i >= knots.size())
        throw std::out_of_range("path knot");
    auto& k = knots[i];
    auto& moved = incoming ? k.handle_in : k.handle_out;
    auto& opposite = incoming ? k.handle_out : k.handle_in;
    if (i == 0 || i + 1 == knots.size()) {
        moved = offset;
    } else {
        const auto opposite_length = length(opposite);
        auto moved_length = length(offset);
        if (moved_length < min_handle_length) {
            // Preserve the previous tangent when the cursor is at the knot.
            const auto old_length = length(moved);
            offset = old_length > min_handle_length ? (1.0 / old_length) * moved :
                opposite_length > min_handle_length ? (-1.0 / opposite_length) * opposite : point{1, 0};
            moved_length = 1;
            offset = min_handle_length * offset;
            moved_length = min_handle_length;
        }
        moved = offset;
        opposite = (-opposite_length / moved_length) * offset;
        // A degenerate opposite handle cannot specify a smooth tangent either.
        if (opposite_length < min_handle_length)
            opposite = (-min_handle_length / moved_length) * offset;
    }
    invalidate();
}

void sm::animation_path::smooth_interior_knots() {
    for (std::size_t i = 1; i + 1 < knots.size(); ++i) {
        const auto incoming = knots[i].handle_in;
        const auto outgoing = knots[i].handle_out;
        // Prefer the incoming tangent when converting legacy corner knots.
        const auto in_length = length(incoming);
    const auto out_length = length(outgoing);
        if (in_length >= min_handle_length) {
            knots[i].handle_out = (-std::max(out_length, min_handle_length) / in_length) * incoming;
        } else if (out_length >= min_handle_length) {
            knots[i].handle_in = (-min_handle_length / out_length) * outgoing;
        } else {
            knots[i].handle_in = {-min_handle_length, 0};
            knots[i].handle_out = {min_handle_length, 0};
        }
    }
    invalidate();
}

bool sm::animation_path::is_smooth() const {
    if (knots.size() < 2)
        return false;
    for (std::size_t i = 1; i + 1 < knots.size(); ++i) {
        const auto a = knots[i].handle_in;
        const auto b = knots[i].handle_out;
        const auto al = length(a);
        const auto bl = length(b);
        if (!(al > 0) || !(bl > 0) || !std::isfinite(al) || !std::isfinite(bl))
            return false;
        const auto cross = a.x * b.y - a.y * b.x;
        if (std::abs(cross) > 1e-9 * al * bl || a.x * b.x + a.y * b.y >= 0)
            return false;
    }
    return true;
}

void sm::animation_path::insert_knot(std::size_t i, double t, point start, point end) {
    if (knots.size() < 2 || i >= knots.size() - 1 || !std::isfinite(t) || t <= 0 || t >= 1)
        throw std::out_of_range("path segment or subdivision parameter");
    const auto a = knot_position(*this, i, start, end);
    const auto b = knot_position(*this, i + 1, start, end);
    const auto p0 = a;
    const auto p1 = a + knots[i].handle_out;
    const auto p2 = b + knots[i + 1].handle_in;
    const auto p3 = b;
    const auto a1 = mix(p0, p1, t);
    const auto a2 = mix(p1, p2, t);
    const auto a3 = mix(p2, p3, t);
    const auto b1 = mix(a1, a2, t);
    const auto b2 = mix(a2, a3, t);
    const auto mid = mix(b1, b2, t);
    knots[i].handle_out = a1 - a;
    knots[i + 1].handle_in = a3 - b;
    knots.insert(knots.begin() + i + 1, path_knot{mid, b1 - mid, b2 - mid});
    invalidate();
}

void sm::animation_path::remove_knot(std::size_t i) {
    if (i == 0 || i + 1 >= knots.size())
        throw std::out_of_range("endpoint knots cannot be removed");
    knots.erase(knots.begin() + i);
    invalidate();
}
