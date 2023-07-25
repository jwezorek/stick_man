#pragma once

#include <string>
#include <vector>
#include <memory>
#include <optional>
#include <functional>
#include <span>
#include <unordered_map>
#include <expected>
#include <tuple>
#include <any>
#include "ik_types.h"

/*------------------------------------------------------------------------------------------------*/

namespace sm {

    namespace detail {
        template <typename T> class enable_protected_make_unique {
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
    }

    class bone;
    class node;

    enum class result {
        no_error,
        multi_parent_node,
        cyclic_bones,
        non_unique_name,
		bone_not_found,
		node_not_found,
		constraint_not_found,
		unknown_error
    };

    using const_bone_ref =  std::reference_wrapper<const bone>;
    using const_node_ref =  std::reference_wrapper<const node>;
    using bone_ref = std::reference_wrapper<bone>;
	using node_ref = std::reference_wrapper<node>;
    using maybe_bone_ref = std::optional<std::reference_wrapper<bone>>;
    using expected_bone = std::expected<bone_ref, result>;
    using expected_node = std::expected<node_ref, result>;

	struct angle_range {
		double min;
		double max;
	};

	struct joint_constraint {
		const_bone_ref anchor_bone;
		const_bone_ref dependent_bone;
		angle_range angles;
	};

    class node : public detail::enable_protected_make_unique<node> {
        friend class ik_sandbox;
        friend class bone;
    private:
        std::string name_;
        double x_;
        double y_;
        maybe_bone_ref parent_;
        std::vector<bone_ref> children_;
        std::any user_data_;
        bool is_pinned_;
		std::vector<joint_constraint> constraints_;

    protected:

        node(const std::string& name, double x, double y);
        void set_parent(bone& b);
        void add_child(bone& b);

    public:
        std::string name() const;
        maybe_bone_ref parent_bone();
        std::span<bone_ref> child_bones();
        double world_x() const;
        double world_y() const;
        void set_world_pos(const point& pt);
        point world_pos() const;
        std::any get_user_data() const;
        bool is_root() const;
        bool is_pinned() const;
		std::optional<angle_range> constraint_angles(const bone& parent, const bone& dependent) const;
        
		void set_user_data(std::any data);
        void set_pinned(bool pinned);
		result set_constraint(const bone& parent, const bone& dependent, double min, double max);
		result remove_constraint(const bone& parent, const bone& dependent);
    };

    class bone : public detail::enable_protected_make_unique<bone> {
        friend class ik_sandbox;
    private:
        std::string name_;
        node& u_;
        node& v_;
        double length_;
        std::any user_data_;

    protected:

        bone(std::string name, node& u, node& v);

    public:
        std::string name() const;
        node& parent_node() const;
        node& child_node() const;
        node& opposite_node(const node& j) const;

        std::tuple<point, point> line_segment() const;
        double length() const;
        double scaled_length() const;
        double world_rotation() const;
        double rotation() const;
        double scale() const;
        double absolute_scale() const;
        maybe_bone_ref parent_bone() const;
        std::any get_user_data() const;
        void set_user_data(std::any data);

        void rotate(double theta);
    };

    using node_visitor = std::function<bool(node&)>;
    using bone_visitor = std::function<bool(bone&)>;

    class ik_sandbox {
    private:
        int node_id_;
        int bone_id_;
        std::unordered_map<std::string, std::unique_ptr<node>> nodes_;
        std::unordered_map<std::string, std::unique_ptr<bone>> bones_;

    public:
        ik_sandbox();
        expected_node create_node(const std::string& name, double x, double y);
        bool set_node_name(node& j, const std::string& name);
        expected_bone create_bone(const std::string& name, node& u, node& v);
        bool set_bone_name(bone& b, const std::string& name);
        bool is_reachable(node& j1, node& j2);
    };

    void dfs(node& j1, node_visitor visit_node = {}, bone_visitor visit_bone = {},
        bool just_downstream = false);

    void visit_nodes(node& j, node_visitor visit_node);

    void debug_reach(node& j, sm::point pt);
    void perform_fabrik(node& j, const sm::point& pt, double tolerance = 0.005, int max_iter = 100);

	point apply_angle_constraint(const point& fixed_pt1, const point& fixed_pt2, const point& free_pt,
		double min_angle, double max_angle, bool fixed_bone_is_anchor);

	double apply_parent_child_constraint(const bone& parent, double rotation, double min_angle, double max_angle);
}