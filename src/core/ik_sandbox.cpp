#include "ik_sandbox.h"
#include <cmath>

/*------------------------------------------------------------------------------------------------*/

namespace {

    double distance(const sm::point& u, const sm::point& v) {
        auto x_diff = u.x - v.x;
        auto y_diff = u.y - v.y;
        return std::sqrt(x_diff * x_diff + y_diff * y_diff);
    }

}

/*------------------------------------------------------------------------------------------------*/

sm::point sm::point::operator += (const point& p) {
    *this = *this + p;
    return *this;
}

sm::point sm::point::operator-=(const point& p) {
    *this = *this - p;
    return *this;
}

sm::point sm::operator+(const sm::point& p1, const sm::point& p2) {
    return {
        p1.x + p2.x,
        p1.y + p2.y
    };
}

sm::point sm::operator-(const sm::point& p1, const sm::point& p2) {
    return {
        p1.x - p2.x,
        p1.y - p2.y
    };
}

/*------------------------------------------------------------------------------------------------*/

sm::joint::joint(const std::string& name, double x, double y) :
    name_(name),
    x_(x),
    y_(y)
{}

void sm::joint::set_parent(bone& b) {
    parent_ = std::ref(b);
}

std::string sm::joint::name() const {
    return name_;
}

sm::maybe_bone_ref sm::joint::parent_bone() const {
    return parent_;
}

std::span<sm::const_bone_ref> sm::joint::child_bones() const {
    return children_;
}

double sm::joint::world_x() const {
    return x_;
}

double sm::joint::world_y() const {
    return y_;
}

sm::point sm::joint::world_pos() const {
    return { x_, y_ };
}

sm::point sm::joint::pos() const {
    if (!parent_) {
        return world_pos();
    }
    auto origin = parent_->get().parent_joint().world_pos();
    return world_pos() - origin;
}

std::any sm::joint::get_user_data() const {
    return user_data_;
}

void sm::joint::set_user_data(std::any data) {
    user_data_ = data;
}

/*------------------------------------------------------------------------------------------------*/

sm::bone::bone(std::string name, sm::joint& u, sm::joint& v) :
       name_(name), u_(u), v_(v) {
    v.set_parent(*this);
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

std::tuple<sm::point, sm::point> sm::bone::line_segment() const {
    return { u_.world_pos(), v_.world_pos() };
}

double sm::bone::length() const {
    return std::apply(distance, line_segment());
}

double sm::bone::world_rotation() const {
    auto [u, v] = line_segment();
    return std::atan2(
        (v.y - u.y),
        (v.x - u.x)
    );
}

double sm::bone::rotation() const {
    auto parent = parent_bone();
    if (!parent) {
        return world_rotation();
    }
    return world_rotation() - parent->get().world_rotation();
}

sm::maybe_bone_ref  sm::bone::parent_bone() const {
    return u_.parent_bone();
}

std::any sm::bone::get_user_data() const {
    return user_data_;
}

void sm::bone::set_user_data(std::any data) {
    user_data_ = data;
}

/*------------------------------------------------------------------------------------------------*/

sm::ik_sandbox::ik_sandbox() : joint_id_(0), bone_id_(0) {};

std::expected<sm::const_joint_ref, sm::result> sm::ik_sandbox::create_joint(
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

std::expected<sm::const_bone_ref, sm::result> sm::ik_sandbox::create_bone(
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