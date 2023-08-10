#pragma once

#include <Eigen/Dense>

namespace sm {

    struct point {
        double x;
        double y;

        point operator+=(const point& p);
        point operator-=(const point& p);
    };

    point operator+(const point& p1, const point& p2);
    point operator-(const point& p1, const point& p2);
    point operator-(const point& p);
    point operator*(double k, const point& p);
    point operator*(const point& p, double k);

    bool operator==(const point& p1, const point& p2);

    using matrix = Eigen::Matrix<double, 3, 3>;

    point transform(const point& pt, const matrix& mat);
    matrix rotation_matrix(double theta);
    matrix rotation_matrix(double cos_theta, double sin_theta);
    matrix scale_matrix(double x_scale, double y_scale);
    matrix scale_matrix(double scale);
    matrix translation_matrix(double x, double y);
    matrix translation_matrix(const point& pt);
    matrix identity_matrix();
    double distance(const point& u, const point& v);
	double normalize_angle(double theta);
	double angular_distance(double theta1, double theta2);
	sm::matrix rotate_about_point_matrix(const sm::point& pt, double theta);
	double angle_from_u_to_v(const sm::point& u, const sm::point& v);

	struct angle_range {
		double start_angle;
		double span_angle;
	};

	bool angle_in_range(double theta, const angle_range& range);

	std::vector<sm::angle_range> intersect_angle_ranges(
		const angle_range& a, const angle_range& b);
}