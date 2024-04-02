#include "sm_types.h"
#include "sm_bone.h"
#include "sm_skeleton.h"
#include "sm_fabrik.h"
#include "sm_traverse.h"
#include "qdebug.h"
#include <unordered_map>

using namespace std::placeholders;
namespace r = std::ranges;
namespace rv = std::ranges::views;

/*------------------------------------------------------------------------------------------------*/

namespace {

	template<class... Ts> struct overloaded : Ts... { using Ts::operator()...; };
	template<class... Ts> overloaded(Ts...) -> overloaded<Ts...>;

	struct rotation_info {
		double length;
		double rel_rotation;
		double world_rotation;
	};

	using bone_rotation_tbl = std::unordered_map<sm::bone*, rotation_info>;

	bone_rotation_tbl create_bone_rotation_tbl(
			sm::node& axis, sm::bone& rotating_bone, double theta) {
		bone_rotation_tbl tbl; 
		sm::traverse_bone_hierarchy(axis,
			[&](sm::maybe_bone_ref prev, sm::bone& bone)->sm::visit_result {
				sm::node& u = (prev) ? bone.shared_node(*prev)->get() : axis;
				sm::node& v = bone.opposite_node(u);
				auto world_rot = sm::angle_from_u_to_v(u.world_pos(), v.world_pos());
				auto rel_rot = (prev) ? world_rot - tbl[&prev->get()].world_rotation : world_rot;

				if (&bone == &rotating_bone) {
					rel_rot += theta;
				}

				if (prev.has_value() && (&prev->get() == &rotating_bone) &&
					bone.has_node(axis)) {
					rel_rot -= theta;
				}

				tbl[&bone] = {
					bone.scaled_length(),
					rel_rot,
					world_rot
				};
				return sm::visit_result::continue_traversal;
			}
		); 

		return tbl;
	}
}

sm::node::node(skeleton& parent, const std::string& name, double x, double y) :
	parent_(parent),
	name_(name),
	x_(x),
	y_(y)
{}

void sm::node::set_parent(bone& b) {
	parent_ = std::ref(b);
}

void sm::node::set_name(const std::string& new_name) {
	name_ = new_name;
}

void sm::node::add_child(bone& b) {
	children_.push_back(std::ref(b));
}

std::string sm::node::name() const {
	return name_;
}

sm::expected_node sm::node::copy_to(skeleton& skel) const {
    if (skel.contains<node>(name_)) {
        return std::unexpected(sm::result::non_unique_name);
    }
    auto node = skel.owner().create_node(
        skel, name_,
        x_, y_
    );
    skel.register_node(node.get());
    return node;
}

sm::maybe_bone_ref sm::node::parent_bone() {
	return std::visit(
		overloaded{
			[](skel_ref skel)->maybe_bone_ref {return {}; },
			[](bone_ref bone)->maybe_bone_ref { return bone; }
		},
		parent_
	);
}

// TODO: get rid of duplicate code
sm::maybe_const_bone_ref sm::node::parent_bone() const {
	return std::visit(
		overloaded{
			[](skel_ref skel)->maybe_const_bone_ref {return {}; },
			[](bone_ref bone)->maybe_const_bone_ref { return bone; }
		},
		parent_
	);
}

std::vector<sm::bone_ref> sm::node::child_bones() {
	return children_;
}

std::vector<sm::const_bone_ref> sm::node::child_bones() const {
	return children_ |
		rv::transform([](const auto& c) {return sm::const_bone_ref(c); }) |
		r::to< std::vector<sm::const_bone_ref>>();
}

std::vector<sm::bone_ref> sm::node::adjacent_bones() {
	std::vector<sm::bone_ref> bones;
	bones.reserve(children_.size() + 1);
	if (!is_root()) {
		bones.push_back(*parent_bone());
	}
	r::copy(children_, std::back_inserter(bones));
	bones.shrink_to_fit();
	return bones;
}

std::vector<sm::const_bone_ref> sm::node::adjacent_bones() const {
	auto* non_const_this = const_cast<sm::node*>(this);
	return non_const_this->adjacent_bones() |
		rv::transform([](const auto& c) {return sm::const_bone_ref(c); }) |
		r::to< std::vector<sm::const_bone_ref>>();
}

sm::skeleton& sm::node::owner() {
	return std::visit(
		overloaded{
			[](skel_ref skel)->sm::skeleton& { 
                return skel.get(); 
            },
			[](bone_ref bone)->sm::skeleton& {
				return bone.get().owner();
			}
		},
		parent_
	);
}

const sm::skeleton& sm::node::owner() const {
	auto non_const_this = const_cast<sm::node*>(this);
	return non_const_this->owner();
}

double sm::node::world_x() const {
	return x_;
}

double sm::node::world_y() const {
	return y_;
}

void sm::node::set_world_pos(const point& pt) {
	x_ = pt.x;
	y_ = pt.y;
}

void sm::node::apply(matrix& mat) {
    set_world_pos(
        transform(world_pos(), mat)
    );
}

sm::point sm::node::world_pos() const {
	return { x_, y_ };
}

std::any sm::node::get_user_data() const {
	return user_data_;
}

void sm::node::set_user_data(std::any data) {
	user_data_ = data;
}

void  sm::node::clear_user_data() {
	user_data_.reset();
}

bool sm::node::is_root() const {
	return std::holds_alternative<skel_ref>(parent_);
}

/*------------------------------------------------------------------------------------------------*/

sm::bone::bone(std::string name, sm::node& u, sm::node& v) :
	name_(name), u_(u), v_(v) {
	v.set_parent(*this);
	u.add_child(*this);
	length_ = scaled_length();
}

void sm::bone::set_name(const std::string& new_name) {
	name_ = new_name;
}

sm::result sm::bone::set_rotation_constraint(double start, double span, bool relative_to_parent) {
	if (relative_to_parent && !parent_bone()) {
		return result::no_parent;
	}
	rot_constraint_ = rot_constraint{ relative_to_parent, start, span };
	return result::success;
}

std::optional<sm::rot_constraint> sm::bone::rotation_constraint() const {
	return rot_constraint_;
}

void sm::bone::remove_rotation_constraint() {
	rot_constraint_ = {};
}

std::string sm::bone::name() const {
	return name_;
}

sm::expected_bone sm::bone::copy_to(skeleton& skel) const
{
    if (skel.contains<bone>(name_)) {
        return std::unexpected(result::non_unique_name);
    }

    auto u = skel.get_by_name<node>(parent_node().name());
    auto v = skel.get_by_name<node>(child_node().name());
    if (!u || !v) {
        return std::unexpected(result::no_parent);
    }

    auto bone = skel.owner().create_bone_in_skeleton(name_, u->get(), v->get());
    if (rot_constraint_) {
        bone->get().set_rotation_constraint(
            rot_constraint_->start_angle,
            rot_constraint_->span_angle,
            rot_constraint_->relative_to_parent
        );
    }
    skel.register_bone(bone->get());

    return bone;
}

sm::node& sm::bone::parent_node() {
	return u_;
}

const sm::node& sm::bone::parent_node() const {
	auto* non_const_this = const_cast<sm::bone*>(this);
	return non_const_this->parent_node();
}

sm::node& sm::bone::child_node() {
	return v_;
}

const sm::node& sm::bone::child_node() const {
	auto* non_const_this = const_cast<sm::bone*>(this);
	return non_const_this->child_node();
}

sm::node& sm::bone::opposite_node(const node& j) {
	if (&j == &u_) {
		return v_;
	}
	else {
		return u_;
	}
}

bool sm::bone::has_node(const sm::node& j) const {
	return &u_ == &j || &v_ == &j;
}

std::vector<sm::bone_ref> sm::bone::child_bones() {
	return v_.child_bones();
}

std::vector<sm::const_bone_ref> sm::bone::child_bones() const {
	return const_cast<const sm::node&>(v_).child_bones();
}

std::vector<sm::bone_ref> sm::bone::sibling_bones() {
	return u_.child_bones() |
		rv::filter(
			[this](bone_ref sib) {
				return &sib.get() != this;
			}
	) | r::to<std::vector<sm::bone_ref>>();
}

bool sm::bone::is_sibling(const bone& b) const
{
	return &parent_node() == &b.parent_node();
}

const sm::node& sm::bone::opposite_node(const node& j) const {
	auto* non_const_this = const_cast<sm::bone*>(this);
	return non_const_this->opposite_node(j);
}

sm::skeleton& sm::bone::owner() {
	return parent_node().owner();
}

const sm::skeleton& sm::bone::owner() const {
	return parent_node().owner();
}

sm::maybe_node_ref sm::bone::shared_node(const bone& b) {
	auto* b_u = &b.parent_node();
	auto* b_v = &b.child_node();
	if (&u_ == b_u || (&u_ == b_v)) {
		return u_;
	}
	else if (&v_ == b_u || &v_ == b_v) {
		return v_;
	}
	return {};
}

sm::maybe_const_node_ref sm::bone::shared_node(const bone& b) const {
	auto* non_const_this = const_cast<sm::bone*>(this);
	return non_const_this->shared_node(b);
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

sm::maybe_const_bone_ref  sm::bone::parent_bone() const {
	return u_.parent_bone();
}

sm::maybe_bone_ref  sm::bone::parent_bone() {
	return u_.parent_bone();
}

std::any sm::bone::get_user_data() const {
	return user_data_;
}

void sm::bone::set_user_data(std::any data) {
	user_data_ = data;
}

void sm::bone::clear_user_data() {
	user_data_.reset();
}

/*
void sm::bone::rotate(double theta) {
	set_rotation(
		rotation() + theta
	);
}

void sm::bone::set_rotation(double theta) {
    //TODO
}
*/

void sm::bone::set_world_rotation(double theta) {
	bone_rotation_tbl rotation_tbl;
	visit_bones(*this, 
		[&](bone& b)->visit_result {
			rotation_tbl[&b] = {
				b.scaled_length(),
				b.world_rotation()
			};
			return visit_result::continue_traversal;
		}
	);

    rotation_tbl[this].world_rotation = theta;

	visit_bones(*this,
		[&](bone& b)->visit_result {

			auto theta = rotation_tbl.at(&b).world_rotation;
			theta = sm::constrain_rotation(b, theta);

			auto rotate_about_u = rotate_about_point_matrix(
				b.parent_node().world_pos(), theta);
			auto v = b.parent_node().world_pos() + sm::point{ rotation_tbl.at(&b).length, 0.0 };
			b.child_node().set_world_pos(
				transform(v, rotate_about_u)
			);

			return visit_result::continue_traversal;
		}
	);
}

void sm::bone::rotate_by(double theta, sm::maybe_node_ref axis) {
	if (!axis) {
		axis = std::ref(parent_node());
	}
	auto old_rotation_tbl = create_bone_rotation_tbl(*axis, *this, theta);
	std::unordered_map<sm::bone*, double> new_world_rotation;

	traverse_bone_hierarchy(*axis,
		[&](sm::maybe_bone_ref prev, sm::bone& bone)->sm::visit_result {
			sm::node& u = (prev) ? bone.shared_node(*prev)->get() : axis->get();
			sm::node& v = bone.opposite_node(u);
			auto parent_world_rotation = prev ? new_world_rotation.at(&prev->get()) : 0;
			auto new_v_pos = transform(
				u.world_pos() + sm::point{ old_rotation_tbl.at(&bone).length, 0.0 },
				rotate_about_point_matrix(
					u.world_pos(),
					old_rotation_tbl[&bone].rel_rotation + parent_world_rotation
				)
			);
			new_v_pos = sm::apply_rotation_constraints(new_v_pos, *axis, prev, bone);
			v.set_world_pos(new_v_pos);
			new_world_rotation[&bone] = angle_from_u_to_v(u.world_pos(), v.world_pos());

			return sm::visit_result::continue_traversal;
		}
	);
}

void sm::bone::set_length(double len) {
    std::unordered_map<bone*, std::tuple<double,double>> bone_to_len_and_rot;
    std::vector<bone*> topo_order;
    dfs(*this, {},
        [&](sm::bone& bone)->visit_result {
            bone_to_len_and_rot[&bone] = { bone.length(), bone.world_rotation() };
            topo_order.push_back(&bone);
            return visit_result::continue_traversal;
        },
        true
    );
    bone_to_len_and_rot[this] = { len, world_rotation() };
    for (auto* bone : topo_order) {
        auto [len, rot] = bone_to_len_and_rot.at(bone);
        sm::point offset = {
            len * std::cos(rot),
            len * std::sin(rot)
        };
        auto new_child_node_pos = bone->parent_node().world_pos() + offset;
        bone->child_node().set_world_pos(new_child_node_pos);
    }
}
