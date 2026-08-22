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
#include "sm_object_id.hpp"
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
        using nodes_tbl = std::unordered_map<object_id, node*>;
        using bones_tbl = std::unordered_map<object_id, bone*>;
        const object_id id_;
        world_ref owner_;
        std::string name_;
        maybe_node_ref root_;
        std::any user_data_;
        nodes_tbl nodes_;
        bones_tbl bones_;
        std::vector<animation> animations_;
    protected:
        skeleton(world& w, object_id id);
        skeleton(world& w, object_id id, const std::string& name, double x, double y);
        void on_new_bone(sm::bone& bone);
        void set_name(const std::string& str);
        result from_json(world& w, const nlohmann::json&);
        nlohmann::json to_json() const;
        void set_root(sm::node& new_root);
        void register_node(sm::node& new_node);
        void register_bone(sm::bone& new_bone);
        void set_owner(world& owner);
    public:
        const object_id& id() const noexcept;
        std::string name() const;
        bool empty() const;
        sm::node& root_node();
        const sm::node& root_node() const;

        std::any get_user_data() const;
        void set_user_data(std::any data);
        void clear_user_data();
        // Model snapshots preserve object identity.
        expected_skel copy_to(world& w, const std::string& new_name = "") const;
        // Editor duplication creates fresh identity and remaps internal references.
        expected_skel duplicate_to(world& w, const std::string& new_name = "") const;

        void set_name(bone& bone, const std::string& new_name);
        void set_name(node& node, const std::string& new_name);
        auto nodes() { return detail::to_range_view<node_ref>(nodes_); }
        auto bones() { return detail::to_range_view<bone_ref>(bones_); }
        auto nodes() const { return detail::to_range_view<const_node_ref>(nodes_); }
        auto bones() const { return detail::to_range_view<const_bone_ref>(bones_); }

        const std::vector<animation>& animations() const;
        void insert_animation(const animation& anim);

        sm::world& owner();
        const sm::world& owner() const;
        // Compatibility/display convenience only; never use labels as identity.
        template <is_node_or_bone T>
        bool contains(const std::string& name) const {
            return get_by_name<T>(name).has_value();
        }

        template <is_node_or_bone T>
        bool contains(const object_id& id) const {
            if constexpr (std::is_same_v<T, sm::node>) {
                return nodes_.contains(id);
            } else {
                return bones_.contains(id);
            }
        }

        void apply(matrix& mat);
        template <is_node_or_bone T>
        std::optional<sm::ref<T>> get(const object_id& id) const {
            if constexpr (std::is_same_v<T, sm::node>) {
                auto it = nodes_.find(id);
                if (it != nodes_.end()) {
                    return sm::ref<T>(*it->second);
                }
            } else if constexpr (std::is_same_v<T, sm::bone>) {
                auto it = bones_.find(id);
                if (it != bones_.end()) {
                    return sm::ref<T>(*it->second);
                }
            }
            return {};
        }
        // Compatibility/display lookup only. Names are labels and may be non-unique; this returns the first match.
        template <is_node_or_bone T>
        std::optional<sm::ref<T>> get_by_name(const std::string& name) const {
            if constexpr (std::is_same_v<T, sm::node>) {
                for (const auto& [id, ptr] : nodes_) {
                    if (ptr->name() == name) {
                        return sm::ref<T>(*ptr);
                    }
                }
            } else if constexpr (std::is_same_v<T, sm::bone>) {
                for (const auto& [id, ptr] : bones_) {
                    if (ptr->name() == name) {
                        return sm::ref<T>(*ptr);
                    }
                }
            }
            return {};
        }
    };
    class world {
        friend class skeleton;
        friend class node;
        friend class bone;
    private:
        using skeleton_tbl = std::unordered_map<object_id, std::unique_ptr<skeleton>>;

        std::vector<std::unique_ptr<node>> nodes_;
        std::vector<std::unique_ptr<bone>> bones_;
        skeleton_tbl skeletons_;
        node_ref create_node(skeleton& parent, object_id id, const std::string& name, double x, double y);
        node_ref create_node(skeleton& parent, const std::string& name, double x, double y);
        node_ref create_node(skeleton& parent, double x, double y);
        expected_bone create_bone_in_skeleton(object_id id, const std::string& bone_name, node& u, node& v);
        expected_bone create_bone_in_skeleton(const std::string& bone_name, node& u, node& v);
        expected_skel create_skeleton_with_id(object_id id, const std::string& name);
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
        expected_skel skeleton(const object_id& id);
        expected_const_skel skeleton(const object_id& id) const;
        // Compatibility/display lookup only; these return the first matching label.
        expected_skel skeleton(const std::string& name);
        expected_const_skel skeleton(const std::string& name) const;
        result delete_skeleton(const object_id& id);

        std::vector<std::string> skeleton_names() const;
        bool contains_skeleton(const object_id& id) const;
        // Compatibility/display convenience only; labels are not identity.
        bool contains_skeleton(const std::string& name) const;
        void set_name(sm::skeleton& skel, const std::string& new_name);
        expected_bone create_bone(const std::string& name, node& u, node& v);
        expected_bone create_bone(object_id id, const std::string& name, node& u, node& v);
        result from_json_str(const std::string& js);
        result from_json(const nlohmann::json& js);
        std::string to_json_str() const;
        nlohmann::json to_json() const;
        void apply(matrix& mat);
        auto skeletons() { return detail::to_range_view<skel_ref>(skeletons_); }
        auto skeletons() const { return detail::to_range_view<const_skel_ref>(skeletons_); }
    };

}
