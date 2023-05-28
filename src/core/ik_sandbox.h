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
        non_unique_name
    };

    using const_bone_ref = const std::reference_wrapper<bone>;
    using const_node_ref = const std::reference_wrapper<node>;
    using bone_ref = std::reference_wrapper<bone>;
    using maybe_bone_ref = std::optional<std::reference_wrapper<bone>>;
    using expected_bone = std::expected<const_bone_ref, result>;
    using expected_node = std::expected<const_node_ref, result>;

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
        maybe_bone_ref parent_bone() const;
        std::span<const_bone_ref> child_bones() const;
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
}