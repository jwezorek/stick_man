#include "ik_sandbox.h"
#include <cmath>
#include <variant>
#include <stack>
#include <unordered_set>

namespace r = std::ranges;
namespace rv = std::ranges::views;

/*------------------------------------------------------------------------------------------------*/

namespace {

    using joint_or_bone = std::variant<sm::const_bone_ref, sm::const_joint_ref>;

    class joint_or_bone_visitor {
        sm::joint_visitor joint_visit_;
        sm::bone_visitor bone_visit_;

    public:
        joint_or_bone_visitor(const sm::joint_visitor& j, sm::bone_visitor& b) :
                joint_visit_(j),
                bone_visit_(b) {
        }

        bool operator()(sm::const_joint_ref j_ref) {
            return (joint_visit_) ? joint_visit_(j_ref.get()) : true;
        }

        bool operator()(sm::const_bone_ref b_ref) {
            return (bone_visit_) ? bone_visit_(b_ref.get()) : true;
        }
    };

    class joint_or_bone_neighbors_visitor {
        bool downstream_;
    public:
        joint_or_bone_neighbors_visitor(bool downstream) : 
            downstream_(downstream)
        {}

        std::vector<joint_or_bone> operator()(sm::const_joint_ref j_ref) {
            auto& joint = j_ref.get();
            auto neighbors = joint.child_bones() |
                rv::transform(
                    [](sm::bone_ref child)->joint_or_bone {
                        return child;
                    }
                ) | r::to<std::vector<joint_or_bone>>();
            if (!downstream_) {
                if (joint.parent_bone().has_value()) {
                    neighbors.push_back(joint.parent_bone().value());
                }
            }
            return neighbors;
        }

        std::vector<joint_or_bone> operator()(sm::const_bone_ref b_ref) {
            auto& bone = b_ref.get();
            std::vector<joint_or_bone> neighbors;
            if (!downstream_) {
                neighbors.push_back(std::ref(bone.parent_joint()));
            }
            neighbors.push_back(std::ref(bone.child_joint()));
            return neighbors;
        }
    };

    double distance(const sm::point& u, const sm::point& v) {
        auto x_diff = u.x - v.x;
        auto y_diff = u.y - v.y;
        return std::sqrt(x_diff * x_diff + y_diff * y_diff);
    }

    uint64_t get_id(const joint_or_bone& var) {
        auto ptr = std::visit(
            [](auto val_ref)->void* {
                auto& val = val_ref.get();
                return reinterpret_cast<void*>(&val);
            },
            var
        );
        return reinterpret_cast<uint64_t>(ptr);
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

void sm::joint::add_child(bone& b) {
    children_.push_back(std::ref(b));
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

void sm::joint::set_world_pos(const point& pt) {
    x_ = pt.x;
    y_ = pt.y;
}

sm::point sm::joint::world_pos() const {
    return { x_, y_ };
}

std::any sm::joint::get_user_data() const {
    return user_data_;
}

void sm::joint::set_user_data(std::any data) {
    user_data_ = data;
}

bool sm::joint::is_root() const {
    return !parent_.has_value();
}

/*------------------------------------------------------------------------------------------------*/

sm::bone::bone(std::string name, sm::joint& u, sm::joint& v) :
       name_(name), u_(u), v_(v) {
    v.set_parent(*this);
    u.add_child(*this);
    length_ = scaled_length();
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
    return length_;
}

double sm::bone::scaled_length() const {
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

double sm::bone::scale() const {
    auto parent = parent_bone();
    if (!parent) {
        return absolute_scale();
    }
    return absolute_scale() / parent->get().absolute_scale();
}

double sm::bone::absolute_scale() const {
    return scaled_length() / length();
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

std::expected<sm::const_bone_ref, sm::result> sm::ik_sandbox::create_bone(
        const std::string& bone_name, joint& u, joint& v) {

    if (!v.is_root()) {
        return std::unexpected(sm::result::multi_parent_joint);
    }

    if (is_reachable(u, v)) {
        return std::unexpected(sm::result::cyclic_bones);
    }

    auto name = (bone_name.empty()) ? "bone-" + std::to_string(++bone_id_) : bone_name;
    if (bones_.contains(name)) {
        return std::unexpected{ sm::result::non_unique_name };
    }

    bones_[name] = bone::make_unique(name, u, v);
    return std::ref( *bones_[name] );
}

bool sm::ik_sandbox::set_bone_name(sm::bone& b, const std::string& name) {
    //TODO
    return false;
}

bool sm::ik_sandbox::is_reachable(joint& j1, joint& j2) {
    auto found = false;
    auto visit_joint = [&found, &j2](joint& j) {
        if (&j == &j2) {
            found = true;
            return false;
        }
        return true;
    };
    dfs(j1, visit_joint);
    return found;
}

void sm::ik_sandbox::dfs(joint& j1, joint_visitor visit_joint, bone_visitor visit_bone,
        bool just_downstream) {
    std::stack<joint_or_bone> stack;
    std::unordered_set<uint64_t> visited;
    joint_or_bone_visitor visitor(visit_joint, visit_bone);
    joint_or_bone_neighbors_visitor neighbors_visitor(just_downstream);
    stack.push(std::ref(j1));

    while (!stack.empty()) {
        auto node = stack.top();
        stack.pop();
        auto id = get_id(node);
        if (visited.contains(id)) {
            continue;
        }
        auto result = std::visit(visitor, node);
        visited.insert(id);
        if (!result) {
            return;
        }
        auto neighbors = std::visit(neighbors_visitor, node);
        for (const auto& neighbor : neighbors) {
            stack.push(neighbor);
        }
    }
}