#include "ik_sandbox.h"
#include "ik_types.h"
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

    sm::matrix rotate_about_point(const sm::point& pt, double theta) {
        return sm::translation_matrix(pt) *
            sm::rotation_matrix(theta) *
            sm::translation_matrix(-pt);
    }

    double angle_from_u_to_v(const sm::point& u, const sm::point& v) {
        auto diff = v - u;
        return std::atan2(diff.y, diff.x);
    }

    sm::point point_on_line_at_distance(const sm::point& u, const sm::point& v, double d) {
        auto pt = sm::point{ u.x + d, u.y };
        return sm::transform(pt, rotate_about_point(u, angle_from_u_to_v(u, v)));
    }

    void reach(sm::bone& bone, double length, sm::joint& leader, sm::point target) {
        if (leader.world_pos() != target) {
            leader.set_world_pos(target);
        }
        sm::joint& follower = bone.opposite_joint(leader);
        auto new_follower_pos = point_on_line_at_distance(
            leader.world_pos(), follower.world_pos(), length
        );
        follower.set_world_pos(new_follower_pos);
    }
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

sm::joint& sm::bone::opposite_joint(const joint& j) const {
    if (&j == &u_) {
        return v_;
    } else {
        return u_;
    }
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

void sm::bone::rotate(double theta) {
    auto rotate_mat = rotate_about_point(u_.world_pos(), theta);
    auto rotate_about_u = [&rotate_mat](joint& j)->bool {
        j.set_world_pos(transform(j.world_pos(), rotate_mat));
        return true;
    };
    visit_joints(v_, rotate_about_u);
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
    if (bones_.contains(name)) {
        return false;
    }
    bones_[name] = std::move(bones_[b.name()]);
    bones_.erase(b.name());
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

void sm::dfs(joint& j1, joint_visitor visit_joint, bone_visitor visit_bone,
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

void sm::visit_joints(joint& j, joint_visitor visit_joint) {
    dfs(j, visit_joint, {}, true);
}

void sm::debug_reach(joint& j, sm::point pt) {
    auto maybe_bone_ref = j.parent_bone();
    auto& bone = maybe_bone_ref->get();
    reach(bone, bone.scaled_length(), j, pt);
}