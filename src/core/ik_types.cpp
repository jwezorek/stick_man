#include <cmath>
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

sm::point sm::operator*(double k, point& p) {
    return { k * p.x, k * p.y };
}

sm::point sm::operator*(point& p, double k) {
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