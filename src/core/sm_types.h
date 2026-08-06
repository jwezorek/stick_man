#pragma once

#include <Eigen/Dense>
#include <expected>
#include <optional>
#include <vector>
#include <functional>
#include <ranges>
#include <memory>
#include <variant>

/*------------------------------------------------------------------------------------------------*/

namespace sm {

    template <typename T>
    struct ref : public std::reference_wrapper<T> {

        ref(T& t) : std::reference_wrapper<T>(t) {
        }

        template<typename U>
        ref(ref<U> r) : std::reference_wrapper<T>(r) {

        }

        T* operator->() {
            return &this->get();
        }

        const T* operator->() const {
            return &this->get();
        }

        T* ptr() {
            return &this->get();
        }

        const T* ptr() const {
            return &this->get();
        }

        auto& operator*() {
            return this->get();
        }

        const auto& operator*() const {
            return this->get();
        }
    };

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

	using bone_ref = ref<bone>;
	using node_ref = ref<node>;
	using skel_ref = ref<skeleton>;
	using world_ref = ref<world>;

	using const_bone_ref = ref<const bone>;
	using const_node_ref = ref<const node>;
	using const_skel_ref = ref<const skeleton>;
	using const_world_ref = ref<const world>;
	
	using maybe_bone_ref = std::optional<bone_ref>;
	using maybe_node_ref = std::optional<node_ref>;
    using maybe_skel_ref = std::optional<skel_ref>;
	using maybe_const_node_ref = std::optional<const_node_ref>;
	using maybe_const_bone_ref = std::optional<const_bone_ref>;
    using maybe_const_skel_ref = std::optional<skel_ref>;
	using expected_bone = std::expected<bone_ref, result>;
	using expected_node = std::expected<node_ref, result>;
	using expected_skel = std::expected<skel_ref, result>;
    using expected_const_skel = std::expected<const_skel_ref, result>;

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

    template <typename T>
    concept is_node_or_bone = std::same_as<T, sm::node> || std::same_as<T, sm::bone>;

    template <typename T>
    concept is_node_or_bone_ref = std::same_as<T, sm::node_ref> || std::same_as<T, sm::bone_ref>;

    template <typename T>
    concept is_skel_piece = std::same_as<T, sm::node> ||
        std::same_as<T, sm::bone> ||
        std::same_as<T, sm::skeleton>;

    template<typename V, typename E>
    using node_or_bone = std::variant<sm::ref<V>, sm::ref<E>>;

    template<class... Ts> struct overloaded : Ts... { using Ts::operator()...; };
    template<class... Ts> overloaded(Ts...) -> overloaded<Ts...>;
}