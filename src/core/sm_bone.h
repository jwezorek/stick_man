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

	class node;

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
		bool is_sibling(const bone& b) const;

		const node& parent_node() const;
		const node& child_node() const;
		const node& opposite_node(const node& j) const;

		skeleton& owner();
		const skeleton& owner() const;

		node& parent_node();
		node& child_node();
		node& opposite_node(const node& j);
		bool has_node(const node& j) const;

		maybe_const_node_ref shared_node(const bone& b) const;
		maybe_node_ref shared_node(const bone& b);

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

		void set_world_rotation(double theta);
		void rotate_by(double theta, sm::maybe_node_ref axis = {}, bool just_this_bone = false);
		void set_length(double len);
	};

	// Returns the first bone adjacent to node `u` that lies on a directed path from `u` to `v`.
	// Traverses downstream from each candidate bone and succeeds if `v` is reachable.

	sm::maybe_bone_ref find_bone_from_u_to_v(sm::node_ref u, sm::node_ref v);

}