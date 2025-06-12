#pragma once

#include <string>
#include <vector>
#include <memory>
#include <optional>
#include <functional>
#include <span>
#include <map>
#include <expected>
#include <tuple>
#include <any>
#include <ranges>
#include <variant>
#include "sm_types.h"
#include "sm_node.h"
#include "sm_bone.h"
#include "sm_animation.h"
#include "json_fwd.hpp"

/*------------------------------------------------------------------------------------------------*/

namespace sm {

	class world;

	class skeleton : public detail::enable_protected_make_unique<skeleton> {
		friend class world;
        friend class node;
        friend class bone;
	private:

        using nodes_tbl = std::map<std::string, node*>;
        using bones_tbl = std::map<std::string, bone*>;

		world_ref owner_;
		std::string name_;
		maybe_node_ref root_;
		std::any user_data_;
        nodes_tbl nodes_;
		bones_tbl bones_;
        std::vector<animation> animations_;

	protected:
        skeleton(world& w);
		skeleton(world& w, const std::string& name, double x, double y);
		void on_new_bone(sm::bone& bone);
		void set_name(const std::string& str);
        result from_json(world& w, const nlohmann::json&);
        nlohmann::json to_json() const;
        void set_root(sm::node& new_root);
        void register_node(sm::node& new_node);
        void register_bone(sm::bone& new_bone);
        void set_owner(world& owner);

	public:
		std::string name() const;
        bool empty() const;
		sm::node& root_node();
		const sm::node& root_node() const;

		std::any get_user_data() const;
		void set_user_data(std::any data);
		void clear_user_data();
        expected_skel copy_to(world& w, const std::string& new_name = "") const;

		result set_name(bone& bone, const std::string& new_name);
		result set_name(node& node, const std::string& new_name);
		
		auto nodes() { return detail::to_range_view<node_ref>(nodes_); }
		auto bones() { return detail::to_range_view<bone_ref>(bones_); }
		auto nodes() const { return detail::to_range_view<const_node_ref>(nodes_); }
		auto bones() const { return detail::to_range_view<const_bone_ref>(bones_); }

        pose current_pose() const;
        void set_pose(const pose& pose);

        const std::vector<animation>& animations() const;
        const animation& get_animation(const std::string& animation) const;
        void insert_animation(const animation& anim);

		sm::world& owner();
		const sm::world& owner() const;

		template <is_node_or_bone T>
		bool contains(const std::string& name) const {
            return get_by_name<T>(name).has_value();
		}

        void apply(matrix& mat);

        template <is_node_or_bone T>
        std::optional<sm::ref<T>> get_by_name(const std::string& name) const {
            if constexpr (std::is_same<T, sm::node>::value) {
                return nodes_.contains(name) ? sm::ref(*nodes_.at(name)) : 
                    std::optional<sm::ref<T>>{};
            } else if constexpr (std::is_same<T, sm::bone>::value) {
                return bones_.contains(name) ? sm::ref(*bones_.at(name)) : 
                    std::optional<sm::ref<T>>{};
            } else {
                static_assert(std::is_same<T, T>::value,
                    "skeleton::get_by_name, unsupported type");
            }
        }
	};

	std::vector<sm::skel_ref> skeletons_from_nodes(const std::vector<sm::node_ref>& nodes);

}