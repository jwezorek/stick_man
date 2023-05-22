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
    class joint;

    enum class result {
        no_error,
        multi_parent_joint,
        cyclic_bones,
        non_unique_name
    };

    using const_bone_ref = const std::reference_wrapper<bone>;
    using const_joint_ref = const std::reference_wrapper<joint>;
    using bone_ref = std::reference_wrapper<bone>;
    using maybe_bone_ref = std::optional<std::reference_wrapper<bone>>;
    using expected_bone = std::expected<const_bone_ref, result>;
    using expected_joint = std::expected<const_joint_ref, result>;

    class joint : public detail::enable_protected_make_unique<joint> {
        friend class ik_sandbox;
        friend class bone;
    private:
        std::string name_;
        double x_;
        double y_;
        maybe_bone_ref parent_;
        std::vector<bone_ref> children_;
        std::any user_data_;
    protected:

        joint(const std::string& name, double x, double y);
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
        void set_user_data(std::any data);
        bool is_root() const;
    };

    class bone : public detail::enable_protected_make_unique<bone> {
        friend class ik_sandbox;
    private:
        std::string name_;
        joint& u_;
        joint& v_;
        double length_;
        std::any user_data_;

    protected:

        bone(std::string name, joint& u, joint& v);

    public:
        std::string name() const;
        joint& parent_joint() const;
        joint& child_joint() const;
        joint& opposite_joint(const joint& j) const;

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

    using joint_visitor = std::function<bool(joint&)>;
    using bone_visitor = std::function<bool(bone&)>;

    class ik_sandbox {
    private:
        int joint_id_;
        int bone_id_;
        std::unordered_map<std::string, std::unique_ptr<joint>> joints_;
        std::unordered_map<std::string, std::unique_ptr<bone>> bones_;

    public:
        ik_sandbox();
        expected_joint create_joint(const std::string& name, double x, double y);
        bool set_joint_name(joint& j, const std::string& name);
        expected_bone create_bone(const std::string& name, joint& u, joint& v);
        bool set_bone_name(bone& b, const std::string& name);
        bool is_reachable(joint& j1, joint& j2);
    };

    void dfs(joint& j1, joint_visitor visit_joint = {}, bone_visitor visit_bone = {},
        bool just_downstream = false);

    void visit_joints(joint& j, joint_visitor visit_joint);

    void debug_reach(joint& j, sm::point pt);
}