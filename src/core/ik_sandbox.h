#pragma once

#include <string>
#include <vector>
#include <memory>
#include <optional>
#include <functional>
#include <span>
#include <unordered_map>
#include <expected>

/*------------------------------------------------------------------------------------------------*/

namespace sm {

    namespace detail{
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

    enum class result {
        no_error,
        multi_parent_joint,
        cyclic_bones,
        non_unique_name
    };

    class bone;

    class joint : public detail::enable_protected_make_unique<joint> {
        friend class ik_sandbox;
    private:
        std::string name_;
        double x_;
        double y_;
        std::optional<const std::reference_wrapper<bone>> parent_;
        std::vector<std::reference_wrapper<bone>> children_;

    protected:

        joint(const std::string& name, double x, double y);

    public:
        std::string name() const;
        std::optional<const std::reference_wrapper<bone>> parent_bone() const;
        std::span<const std::reference_wrapper<bone>> child_bones() const;
        double x() const;
        double y() const;
    };

    class bone : public detail::enable_protected_make_unique<bone> {
        friend class ik_sandbox;
    private:
        std::string name_;
        joint& u_;
        joint& v_;

    protected:

        bone(std::string name, joint& u, joint& v);

    public:
        std::string name() const;
        joint& parent_joint() const;
        joint& child_joint() const;
    };


    class ik_sandbox {
    private:
        int joint_id_;
        int bone_id_;
        std::unordered_map<std::string, std::unique_ptr<joint>> joints_;
        std::unordered_map<std::string, std::unique_ptr<bone>> bones_;

        result test_bone_location(joint& u, joint& v) const;

    public:
        ik_sandbox();
        std::expected<const std::reference_wrapper<joint>, result> create_joint(
            const std::string& name, double x, double y);
        bool set_joint_name(joint& j, const std::string& name);
        std::expected<const std::reference_wrapper<bone>, result> create_bone(
            const std::string& name, joint& u, joint& v);
        bool set_bone_name(bone& b, const std::string& name);

    };
}