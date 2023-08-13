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
		not_found,
		no_parent,
		out_of_bounds,
		unknown_error
    };

    using const_bone_ref =  std::reference_wrapper<const bone>;
    using const_node_ref =  std::reference_wrapper<const node>;
    using bone_ref = std::reference_wrapper<bone>;
	using node_ref = std::reference_wrapper<node>;
    using maybe_bone_ref = std::optional<bone_ref>;
	using maybe_node_ref = std::optional<node_ref>;
	using maybe_const_node_ref = std::optional<const_node_ref>;
	using maybe_const_bone_ref = std::optional<const_bone_ref>;
    using expected_bone = std::expected<bone_ref, result>;
    using expected_node = std::expected<node_ref, result>;;

	struct rot_constraint {
		bool relative_to_parent;
		double start_angle;
		double span_angle;
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

    protected:

        node(const std::string& name, double x, double y);
        void set_parent(bone& b);
        void add_child(bone& b);

    public:
        std::string name() const;

		maybe_bone_ref parent_bone();
        maybe_const_bone_ref parent_bone() const;

        std::vector<const_bone_ref> child_bones() const;
		std::vector<bone_ref> child_bones();

		std::vector<const_bone_ref> adjacent_bones() const;
		std::vector<bone_ref> adjacent_bones();

        double world_x() const;
        double world_y() const;
        void set_world_pos(const point& pt);
        point world_pos() const;

        std::any get_user_data() const;
        bool is_root() const;
        bool is_pinned() const;
        
		void set_user_data(std::any data);
        void set_pinned(bool pinned);
    };

    class bone : public detail::enable_protected_make_unique<bone> {
        friend class ik_sandbox;
    private:

        std::string name_;
        node& u_;
        node& v_;
        double length_;
        std::any user_data_;
		std::optional<rot_constraint> rot_constraint_;

    protected:

        bone(std::string name, node& u, node& v);

    public:
        std::string name() const;

		maybe_const_bone_ref parent_bone() const;
		maybe_bone_ref parent_bone();

		std::vector<bone_ref> child_bones();
		std::vector<const_bone_ref> child_bones() const;

		std::vector<bone_ref> sibling_bones();

        const node& parent_node() const;
		const node& child_node() const;
		const node& opposite_node(const node& j) const;

		node& parent_node();
		node& child_node();
		node& opposite_node(const node& j);

		std::optional<const_node_ref> shared_node(const bone& b) const;
		std::optional<node_ref> shared_node(const bone& b);

		std::optional<rot_constraint> rotation_constraint() const;
		result set_rotation_constraint(double start, double span, bool relative_to_parent);
		void remove_rotation_constraint();

        std::tuple<point, point> line_segment() const;
        double length() const;
        double scaled_length() const;
        double world_rotation() const;
        double rotation() const;
        double scale() const;
        double absolute_scale() const;
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

	enum class fabrik_result {
		target_reached,
		converged,
		mixed,
		no_solution_found
	};

	fabrik_result perform_fabrik(node& j, const sm::point& pt, double tolerance = 0.005, int max_iter = 100);

	

}