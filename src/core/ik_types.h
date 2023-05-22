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

}