#include "sm_types.h"
#include "sm_bone.h"
#include "sm_skeleton.h"

using namespace std::placeholders;
namespace r = std::ranges;
namespace rv = std::ranges::views;

/*------------------------------------------------------------------------------------------------*/

namespace {

	template<class... Ts> struct overloaded : Ts... { using Ts::operator()...; };
	template<class... Ts> overloaded(Ts...) -> overloaded<Ts...>;

}

double constrain_rotation(sm::bone& b, double theta);

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

sm::maybe_bone_ref sm::node::parent_bone() {
	return std::visit(
		overloaded{
			[](skeleton_ref skel)->maybe_bone_ref {return {}; },
			[](bone_ref bone)->maybe_bone_ref { return bone; }
		},
		parent_
	);
}

// TODO: get rid of duplicate code
sm::maybe_const_bone_ref sm::node::parent_bone() const {
	return std::visit(
		overloaded{
			[](skeleton_ref skel)->maybe_const_bone_ref {return {}; },
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

sm::skeleton_ref sm::node::owner() {
	return std::visit(
		overloaded{
			[](skeleton_ref skel) {return skel; },
			[](bone_ref bone) {
				return bone.get().owner();
			}
		},
		parent_
	);
}

sm::const_skeleton_ref sm::node::owner() const {
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
	return std::holds_alternative<skeleton_ref>(parent_);
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

sm::result sm::bone::set_rotation_constraint(double min, double max, bool relative_to_parent) {
	if (relative_to_parent && !parent_bone()) {
		return result::no_parent;
	}
	rot_constraint_ = rot_constraint{ relative_to_parent, min, max };
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
	) | r::to< std::vector<sm::bone_ref>>();
}

const sm::node& sm::bone::opposite_node(const node& j) const {
	auto* non_const_this = const_cast<sm::bone*>(this);
	return non_const_this->opposite_node(j);
}

sm::skeleton_ref sm::bone::owner() {
	return parent_node().owner();
}

sm::const_skeleton_ref sm::bone::owner() const {
	return parent_node().owner();
}

std::optional<sm::node_ref> sm::bone::shared_node(const bone& b) {
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

std::optional<sm::const_node_ref> sm::bone::shared_node(const bone& b) const {
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

void sm::bone::rotate(double theta) {
	set_world_rotation(
		world_rotation() + theta
	);
}

void sm::bone::set_world_rotation(double theta) {
	std::unordered_map<bone*, double> rotation_tbl;
	std::unordered_map<bone*, double> length_tbl;
	visit_bones(*this, 
		[&](bone& b)->bool {
			rotation_tbl[&b] = b.rotation();
			length_tbl[&b] = b.scaled_length();
			return true;
		}
	);

	rotation_tbl[this] = parent_bone().transform(
			[theta](bone_ref br) { return theta - br.get().world_rotation(); }
		).value_or(theta);

	visit_bones(*this,
		[&](bone& b)->bool {
			auto parent_rot = b.parent_bone().transform(
					[](bone_ref br) {return br.get().world_rotation(); }
				).value_or(0.0);
			auto theta = parent_rot + rotation_tbl.at(&b);
			theta = constrain_rotation(b, theta);
			auto rotate_about_u = rotate_about_point_matrix(
				b.parent_node().world_pos(), theta);
			auto v = b.parent_node().world_pos() + sm::point{ length_tbl.at(&b), 0.0 };
			b.child_node().set_world_pos(
				transform(v, rotate_about_u)
			);
			return true;
		}
	);
}
