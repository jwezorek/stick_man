#pragma once

#include "sm_types.h"
#include <variant>
#include <optional>
#include <ranges>
#include <vector>
#include <string>
#include <any>

/*------------------------------------------------------------------------------------------------*/

namespace sm {

	class node : public detail::enable_protected_make_unique<node> {
		friend class world;
		friend class bone;
		friend class skeleton;
	private:
		std::string name_;
		double x_;
		double y_;
		std::variant<skel_ref, bone_ref> parent_;
		std::vector<bone_ref> children_;
		std::any user_data_;

	protected:

		node(skeleton& parent, const std::string& name, double x, double y);
		void set_parent(bone& b);
		void add_child(bone& b);
		void set_name(const std::string& new_name);

	public:
		std::string name() const;
        expected_node copy_to(skeleton& skel) const;

		maybe_bone_ref parent_bone();
		maybe_const_bone_ref parent_bone() const;

		std::vector<const_bone_ref> child_bones() const;
		std::vector<bone_ref> child_bones();

		std::vector<const_bone_ref> adjacent_bones() const;
		std::vector<bone_ref> adjacent_bones();

 		skel_ref owner();
		const_skel_ref owner() const;

		double world_x() const;
		double world_y() const;
		void set_world_pos(const point& pt);
		point world_pos() const;
        void apply(matrix& mat);

		std::any get_user_data() const;
		void set_user_data(std::any data);
		void clear_user_data();

		bool is_root() const;
	};

	class bone : public detail::enable_protected_make_unique<bone> {
		friend class world;
		friend class skeleton;
	private:

		std::string name_;
		node& u_;
		node& v_;
		double length_;
		std::any user_data_;
		std::optional<rot_constraint> rot_constraint_;

	protected:

		bone(std::string name, node& u, node& v);
		void set_name(const std::string& new_name);

	public:
		std::string name() const;
        expected_bone copy_to(skeleton& skel) const;

		maybe_const_bone_ref parent_bone() const;
		maybe_bone_ref parent_bone();

		std::vector<bone_ref> child_bones();
		std::vector<const_bone_ref> child_bones() const;

		std::vector<bone_ref> sibling_bones();

		const node& parent_node() const;
		const node& child_node() const;
		const node& opposite_node(const node& j) const;

		skel_ref owner();
		const_skel_ref owner() const;

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
		void clear_user_data();

		void rotate(double theta);
		void set_world_rotation(double theta);
        void set_rotation(double theta);
        void set_length(double len);
	};

}