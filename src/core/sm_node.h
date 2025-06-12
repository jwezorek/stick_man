#pragma once

#include "sm_types.h"
#include <any>

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

		skeleton& owner();
		const skeleton& owner() const;

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

}