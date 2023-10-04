#pragma once

#include <Eigen/Dense>
#include <expected>
#include <optional>
#include <vector>
#include <functional>
#include <ranges>
#include <memory>

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

	struct rot_constraint {
		bool relative_to_parent;
		double start_angle;
		double span_angle;
	};

	bool angle_in_range(double theta, const angle_range& range);

	std::vector<sm::angle_range> intersect_angle_ranges(
		const angle_range& a, const angle_range& b);

	enum class result {
		success,
		multi_parent_node,
		cyclic_bones,
		non_unique_name,
		not_found,
		no_parent,
		out_of_bounds,
		invalid_json,
		fabrik_target_reached,
		fabrik_converged,
		fabrik_mixed,
		fabrik_no_solution_found,
        cross_skeleton_bone,
		unknown_error
	};

	class bone;
	class node;
	class skeleton;
	class world;

	using bone_ref = std::reference_wrapper<bone>;
	using node_ref = std::reference_wrapper<node>;
	using skel_ref = std::reference_wrapper<skeleton>;
	using world_ref = std::reference_wrapper<world>;

	using const_bone_ref = std::reference_wrapper<const bone>;
	using const_node_ref = std::reference_wrapper<const node>;
	using const_skel_ref = std::reference_wrapper<const skeleton>;
	using const_world_ref = std::reference_wrapper<const world>;
	
	using maybe_bone_ref = std::optional<bone_ref>;
	using maybe_node_ref = std::optional<node_ref>;
    using maybe_skeleton_ref = std::optional<skel_ref>;
	using maybe_const_node_ref = std::optional<const_node_ref>;
	using maybe_const_bone_ref = std::optional<const_bone_ref>;
    using maybe_const_skel_ref = std::optional<skel_ref>;
	using expected_bone = std::expected<bone_ref, result>;
	using expected_node = std::expected<node_ref, result>;
	using expected_skel = std::expected<skel_ref, result>;

    namespace detail {
        template <typename T> 
        class enable_protected_make_unique {
        protected:
            template <typename... Args>
            static std::unique_ptr<T> make_unique(Args &&... args) {
                class make_unique_enabler : public T {
                public:
                    make_unique_enabler(Args &&... args) :
                        T(std::forward<Args>(args)...) {}
                };
                return std::make_unique<make_unique_enabler>(std::forward<Args>(args)...);
            }
        };

        template<typename T>
        auto to_range_view(auto& tbl) {
            return tbl | std::ranges::views::transform(
                [](auto& pair)->T {
                    return *pair.second;
                }
            );
        }
    }
	
}