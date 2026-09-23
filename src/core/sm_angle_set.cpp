#include "sm_angle_set.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>

namespace sm {
namespace {
constexpr double tau = 2 * std::numbers::pi;
constexpr double tolerance = 16 * std::numeric_limits<double>::epsilon() * tau;

double normalized(double angle) {
    double result = std::fmod(angle, tau);
    if (result < 0) result += tau;
    return result == tau || result == 0 ? 0 : result;
}
}

angle_set::angle_set() : intervals_{{0, tau}} {}

angle_set::angle_set(angle_range range) {
    if (!std::isfinite(range.start_angle) || !std::isfinite(range.span_angle)
        || range.span_angle < 0 || range.span_angle > tau)
        throw std::invalid_argument("angle range must be finite with span in [0, 2*pi]");
    append_arc(range.start_angle, range.span_angle);
    canonicalize();
}

angle_set angle_set::empty() {
    angle_set result;
    result.intervals_.clear();
    return result;
}

void angle_set::append_arc(double start, double span) {
    if (span >= tau) {
        intervals_.push_back({0, tau});
        return;
    }
    start = normalized(start);
    const double end = start + span;
    if (end <= tau) intervals_.push_back({start, end});
    else {
        intervals_.push_back({start, tau});
        intervals_.push_back({0, end - tau});
    }
}

void angle_set::canonicalize() {
    // Store the seam at both 0 and 2*pi so ordinary interval intersection
    // preserves a single shared circular endpoint.
    bool seam = false;
    for (auto part : intervals_) seam |= part.low == 0 || part.high == tau;
    if (seam) {
        intervals_.push_back({0, 0});
        intervals_.push_back({tau, tau});
    }
    std::ranges::sort(intervals_, {}, &interval::low);
    std::vector<interval> merged;
    for (auto part : intervals_) {
        if (merged.empty() || part.low > merged.back().high)
            merged.push_back(part);
        else merged.back().high = std::max(merged.back().high, part.high);
    }
    intervals_ = std::move(merged);
}

bool angle_set::is_empty() const { return intervals_.empty(); }

bool angle_set::contains(double angle) const {
    if (!std::isfinite(angle)) return false;
    angle = normalized(angle);
    for (auto part : intervals_)
        if (angle >= part.low && angle <= part.high) return true;
    return false;
}

angle_set angle_set::intersect(const angle_set& other) const {
    auto result = empty();
    for (auto left : intervals_) {
        for (auto right : other.intervals_) {
            const double low = std::max(left.low, right.low);
            const double high = std::min(left.high, right.high);
            if (low <= high) result.intervals_.push_back({low, high});
        }
    }
    result.canonicalize();
    return result;
}

angle_set angle_set::shifted(double offset) const {
    if (!std::isfinite(offset)) throw std::invalid_argument("angle offset must be finite");
    offset = normalized(offset);
    auto result = empty();
    for (auto part : intervals_) result.append_arc(part.low + offset, part.high - part.low);
    result.canonicalize();
    return result;
}

angle_set angle_set::negated() const {
    auto result = empty();
    for (auto part : intervals_) result.append_arc(-part.high, part.high - part.low);
    result.canonicalize();
    return result;
}

std::optional<double> angle_set::closest_angle(double angle) const {
    if (is_empty() || !std::isfinite(angle)) return std::nullopt;
    angle = normalized(angle);
    if (contains(angle)) return angle;
    double best = 0;
    double distance = std::numeric_limits<double>::infinity();
    for (auto part : intervals_) {
        for (double endpoint : {part.low, part.high}) {
            endpoint = normalized(endpoint);
            const double delta = std::abs(endpoint - angle);
            const double candidate_distance = std::min(delta, tau - delta);
            if (candidate_distance < distance - tolerance
                || (std::abs(candidate_distance - distance) <= tolerance && endpoint < best)) {
                best = endpoint;
                distance = candidate_distance;
            }
        }
    }
    return best;
}
}
