#include <cmath>
#include <numbers>
#include "ik_types.h"

/*------------------------------------------------------------------------------------------------*/

namespace {

    using vec = Eigen::Matrix<double, 3, 1>;

}

sm::point sm::point::operator += (const point& p) {
    *this = *this + p;
    return *this;
}

sm::point sm::point::operator-=(const point& p) {
    *this = *this - p;
    return *this;
}

sm::point sm::operator+(const sm::point& p1, const sm::point& p2) {
    return {
        p1.x + p2.x,
        p1.y + p2.y
    };
}

sm::point sm::operator-(const sm::point& p1, const sm::point& p2) {
    return {
        p1.x - p2.x,
        p1.y - p2.y
    };
}

sm::point sm::operator-(const point& p) {
    return { -p.x, -p.y };
}

sm::point sm::operator*(double k, const point& p) {
    return { k * p.x, k * p.y };
}

sm::point sm::operator*(const point& p, double k) {
    return { k * p.x, k * p.y };
}

bool sm::operator==(const point& p1, const point& p2) {
    return p1.x == p2.x && p1.y == p2.y;
}

/*------------------------------------------------------------------------------------------------*/
sm::point sm::transform(const point& pt, const matrix& mat) {
    vec v;
    v << pt.x, pt.y, 1.0;
    v = mat * v;
    return { v[0], v[1] };
}

sm::matrix sm::rotation_matrix(double theta) {
    return rotation_matrix(std::cos(theta), std::sin(theta));
}
sm::matrix sm::rotation_matrix(double cos_theta, double sin_theta) {
    matrix rotation;
    rotation <<
        cos_theta, -sin_theta, 0,
        sin_theta, cos_theta, 0,
        0, 0, 1;
    return rotation;
}

sm::matrix sm::scale_matrix(double x_scale, double y_scale) {
    matrix scale;
    scale <<
        x_scale, 0, 0,
        0, y_scale, 0,
        0, 0, 1;
    return scale;
}

sm::matrix sm::scale_matrix(double scale) {
    matrix mat;
    mat <<
        scale, 0, 0,
        0, scale, 0,
        0, 0, 1;
    return mat;
}

sm::matrix sm::identity_matrix() {
    return scale_matrix(1.0);
}

sm::matrix sm::translation_matrix(double x, double y) {
    matrix translation;
    translation <<
        1, 0, x,
        0, 1, y,
        0, 0, 1;
    return translation;
}

sm::matrix sm::translation_matrix(const point& pt) {
    return translation_matrix(pt.x, pt.y);
}

double sm::distance(const point& u, const point& v) {
    auto x_diff = u.x - v.x;
    auto y_diff = u.y - v.y;
    return std::sqrt(x_diff * x_diff + y_diff * y_diff);
}

double sm::normalize_angle(double theta) {
	auto cosine = std::cos(theta);
	auto sine = std::sin(theta);
	return std::atan2(sine, cosine);
}

double sm::angular_distance(double from, double to) {
	auto diff = to - from;
	return std::atan2(std::sin(diff), std::cos(diff));
}

sm::matrix sm::rotate_about_point_matrix(const sm::point& pt, double theta) {
	return sm::translation_matrix(pt) *
		sm::rotation_matrix(theta) *
		sm::translation_matrix(-pt);
}

double sm::angle_from_u_to_v(const sm::point& u, const sm::point& v) {
	auto diff = v - u;
	return std::atan2(diff.y, diff.x);
}

std::vector<sm::angle_range> sm::intersect_angle_ranges(
		const angle_range& a, const angle_range& b) {
	std::vector<angle_range> intersections;

	constexpr double k_two_pi = 2.0 * std::numbers::pi;
	double greaterAngle;
	double greaterSweep;
	double originAngle;
	double originSweep;

	if (a.start_angle < b.start_angle) {
		originAngle = a.start_angle;
		originSweep = a.span_angle;
		greaterSweep = b.span_angle;
		greaterAngle = b.start_angle;
	}
	else {
		originAngle = b.start_angle;
		originSweep = b.span_angle;
		greaterSweep = a.span_angle;
		greaterAngle = a.start_angle;
	}
	double greaterAngleRel = greaterAngle - originAngle;
	if (greaterAngleRel < originSweep) {
		intersections.emplace_back(greaterAngle, std::min(greaterSweep, originSweep - greaterAngleRel));
	}
	double rouno = greaterAngleRel + greaterSweep;
	if (rouno > k_two_pi) {
		intersections.emplace_back(originAngle, std::min(rouno - k_two_pi, originSweep));
	}
	return intersections;
}

bool sm::angle_in_range(double theta, const angle_range& range) {
	auto end_angle = range.start_angle + range.span_angle;
	if (end_angle <= std::numbers::pi) {
		return theta >= range.start_angle && theta <= end_angle;
	}
	if (theta >= range.start_angle && theta <= std::numbers::pi) {
		return true;
	}
	auto wrap_around = (end_angle - 2*std::numbers::pi);
	return (theta >= -std::numbers::pi && theta <= wrap_around);
}