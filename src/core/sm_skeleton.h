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
#include <ranges>
#include <variant>
#include "sm_types.h"
#include "sm_bone.h"
#include "json_fwd.hpp"

/*------------------------------------------------------------------------------------------------*/

namespace sm {

	class world;

	class skeleton : public detail::enable_protected_make_unique<skeleton> {
		friend class world;
        friend class node;
        friend class bone;
	private:

        using nodes_tbl = std::unordered_map<std::string, node*>;
        using bones_tbl = std::unordered_map<std::string, bone*>;

		world_ref owner_;
		std::string name_;
		maybe_node_ref root_;
		std::any user_data_;
        nodes_tbl nodes_;
		bones_tbl bones_;

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

		sm::world& owner();
		const sm::world& owner() const;

		template <is_node_or_bone T>
		bool contains(const std::string& name) const {
            return get_by_name<T>(name).has_value();
		}

        void apply(matrix& mat);

        template <is_node_or_bone T>
        std::optional<std::reference_wrapper<T>> get_by_name(const std::string& name) const {
            if constexpr (std::is_same<T, sm::node>::value) {
                return nodes_.contains(name) ? std::ref(*nodes_.at(name)) : 
                    std::optional<std::reference_wrapper<T>>{};
            } else if constexpr (std::is_same<T, sm::bone>::value) {
                return bones_.contains(name) ? std::ref(*bones_.at(name)) : 
                    std::optional<std::reference_wrapper<T>>{};
            } else {
                static_assert(std::is_same<T, T>::value,
                    "skeleton::get_by_name, unsupported type");
            }
        }
	};

    class world {
		friend class skeleton;
        friend class node;
        friend class bone;
    private:
        using skeleton_tbl = std::unordered_map<std::string, std::unique_ptr<skeleton>>;
		
		std::vector<std::unique_ptr<node>> nodes_;
		std::vector<std::unique_ptr<bone>> bones_;
        skeleton_tbl skeletons_;

        node_ref create_node(skeleton& parent, const std::string& name, double x, double y);
		node_ref create_node(skeleton& parent, double x, double y);
        expected_bone create_bone_in_skeleton(const std::string& bone_name, node& u, node& v);

    public:
		world();
        world(world&& other);
        world& operator=(world&& other);
        world(const world& other) = delete;
        world& operator=(const world& other) = delete;
        ~world() = default;

        void clear();
        bool empty() const;
		skeleton& create_skeleton(double x, double y);
        skeleton& create_skeleton(const point& pt);
        expected_skel create_skeleton(const std::string& name);
		expected_skel skeleton(const std::string& name);
        expected_const_skel skeleton(const std::string& name) const;

        result delete_skeleton(const std::string& skel_name);

		std::vector<std::string> skeleton_names() const;
		bool contains_skeleton(const std::string& name) const;
        result set_name(sm::skeleton& skel, const std::string& new_name);

        expected_bone create_bone(const std::string& name, node& u, node& v);
        result from_json_str(const std::string& js);
		result from_json(const nlohmann::json& js);
		std::string to_json_str() const;
        nlohmann::json to_json() const;
        void apply(matrix& mat);

		auto skeletons() { return detail::to_range_view<skel_ref>(skeletons_); }
		auto skeletons() const { return detail::to_range_view<const_skel_ref>(skeletons_); }
    };

}