#include "ik_sandbox.h"

/*------------------------------------------------------------------------------------------------*/

namespace {



}

sm::joint::joint(const std::string& name, double x, double y) :
    name_(name),
    x_(x),
    y_(y)
{}

std::string sm::joint::name() const {
    return name_;
}

std::optional<const std::reference_wrapper<sm::bone>> sm::joint::parent_bone() const {
    return parent_;
}

std::span<const std::reference_wrapper<sm::bone>> sm::joint::child_bones() const {
    return children_;
}

double sm::joint::x() const {
    return x_;
}

double sm::joint::y() const {
    return y_;
}

/*------------------------------------------------------------------------------------------------*/

sm::bone::bone(std::string name, sm::joint& u, sm::joint& v) :
       name_(name), u_(u), v_(v) {

}

std::string sm::bone::name() const {
    return name_;
}

sm::joint& sm::bone::parent_joint() const {
    return u_;
}

sm::joint& sm::bone::child_joint() const {
    return v_;
}

/*------------------------------------------------------------------------------------------------*/

sm::ik_sandbox::ik_sandbox() : joint_id_(0), bone_id_(0) {};

std::expected<const std::reference_wrapper<sm::joint>, sm::result> sm::ik_sandbox::create_joint(
        const std::string& joint_name, double x, double y) {
    auto name = (joint_name.empty()) ? "joint-" + std::to_string(++joint_id_) : joint_name;
    if (joints_.contains(name)) {
        return std::unexpected{ sm::result::non_unique_name };
    }
    joints_[name] = joint::make_unique(name, x, y);
    return std::ref( *joints_[name] );
}

bool sm::ik_sandbox::set_joint_name(joint& j, const std::string& name) {
    if (joints_.contains(name)) {
        return false;
    }
    joints_[name] = std::move(joints_[j.name()]);
    joints_.erase(j.name());
    return true;
}

sm::result sm::ik_sandbox::test_bone_location(joint& u, joint& v) const {
    return sm::result::no_error;
}

std::expected<const std::reference_wrapper<sm::bone>, sm::result> sm::ik_sandbox::create_bone(
        const std::string& bone_name, joint& u, joint& v) {
    auto name = (bone_name.empty()) ? "bone-" + std::to_string(++bone_id_) : bone_name;
    if (bones_.contains(name)) {
        return std::unexpected{ sm::result::non_unique_name };
    }

    if (auto res = test_bone_location(u, v); res != sm::result::no_error) {
        return std::unexpected{ res };
    }

    bones_[name] = bone::make_unique(name, u, v);
    return std::ref( *bones_[name] );
}

bool sm::ik_sandbox::set_bone_name(sm::bone& b, const std::string& name) {
    return false;
}