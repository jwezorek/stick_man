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

		template<typename T>
		auto to_range_view(auto& tbl) {
			return tbl | std::ranges::views::transform(
				[](auto& pair)->T {
					return *pair.second;
				}
			);
		}
	}

	class node : public detail::enable_protected_make_unique<node> {
		friend class world;
		friend class bone;
		friend class skeleton;
	private:
		std::string name_;
		double x_;
		double y_;
		std::variant<skeleton_ref, bone_ref> parent_;
		std::vector<bone_ref> children_;
		std::any user_data_;

	protected:

		node(skeleton& parent, const std::string& name, double x, double y);
		void set_parent(bone& b);
		void add_child(bone& b);
		void set_name(const std::string& new_name);

	public:
		std::string name() const;

		maybe_bone_ref parent_bone();
		maybe_const_bone_ref parent_bone() const;

		std::vector<const_bone_ref> child_bones() const;
		std::vector<bone_ref> child_bones();

		std::vector<const_bone_ref> adjacent_bones() const;
		std::vector<bone_ref> adjacent_bones();

		skeleton_ref owner();
		const_skeleton_ref owner() const;

		double world_x() const;
		double world_y() const;
		void set_world_pos(const point& pt);
		point world_pos() const;

		std::any get_user_data() const;
		bool is_root() const;

		void set_user_data(std::any data);
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

		maybe_const_bone_ref parent_bone() const;
		maybe_bone_ref parent_bone();

		std::vector<bone_ref> child_bones();
		std::vector<const_bone_ref> child_bones() const;

		std::vector<bone_ref> sibling_bones();

		const node& parent_node() const;
		const node& child_node() const;
		const node& opposite_node(const node& j) const;

		skeleton_ref owner();
		const_skeleton_ref owner() const;

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

}