#include "sm_node.h"
#include "sm_bone.h"
#include "sm_skeleton.h"
#include "sm_world.h"
#include <ranges>
#include <variant>

namespace r = std::ranges;
namespace rv = std::ranges::views;

/*------------------------------------------------------------------------------------------------*/

sm::node::node(skeleton& parent, const std::string& name, double x, double y) :
	parent_(parent),
	name_(name),
	x_(x),
	y_(y)
{
}

void sm::node::set_parent(bone& b) {
	parent_ = sm::ref(b);
}

void sm::node::set_name(const std::string& new_name) {
	name_ = new_name;
}

void sm::node::add_child(bone& b) {
	children_.push_back(sm::ref(b));
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
	skel.register_node(node);
	return node;
}

sm::maybe_bone_ref sm::node::parent_bone() {
	return std::visit(
		overloaded{
			[](skel_ref skel)->maybe_bone_ref { return {}; },
			[](bone_ref bone)->maybe_bone_ref { return bone; }
		},
		parent_
	);
}

// TODO: get rid of duplicate code
sm::maybe_const_bone_ref sm::node::parent_bone() const {
	return std::visit(
		overloaded{
			[](skel_ref skel)->maybe_const_bone_ref { return {}; },
			[](bone_ref bone)->maybe_const_bone_ref {
				return const_bone_ref(bone.get());
			}
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
				return *skel;
			},
			[](bone_ref bone)->sm::skeleton& {
				return bone->owner();
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