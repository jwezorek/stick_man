#include "canvas_item.h"
#include "canvas.h"
#include "util.h"
#include "../core/ik_sandbox.h"

namespace {

	constexpr double k_node_radius = 8.0;
	constexpr int k_node_zorder = 10;
	constexpr double k_pin_radius = k_node_radius - 3.0;
	constexpr double k_sel_frame_distance = 4.0;
	constexpr double k_joint_constraint_radius = 50.0;
	const auto k_sel_color = QColorConstants::Svg::turquoise;
	constexpr double k_sel_thickness = 3.0;
	constexpr int k_bone_zorder = 5;

	void set_circle(QGraphicsEllipseItem* ei, QPointF pos, double radius, double scale) {
		ei->setPos(0, 0);
		double r = radius * scale;
		ei->setRect(
			QRectF(
				-r, -r, 2.0 * r, 2.0 * r
			)
		);
		ei->setPos(pos);
	}

	QPolygonF bone_polygon(double length, double node_radius, double scale) {
		auto r = static_cast<float>(node_radius * scale);
		auto d = static_cast<float>(length);
		auto x1 = (r * r) / d;
		auto y1 = r * std::sqrt(1.0f - (r * r) / (d * d));
		QList<QPointF> pts = {
			{0,0}, {x1, y1 }, {d, 0}, {x1, -y1}, {0,0}
		};
		return { pts };
	}

	void set_bone_item_pos(
		ui::bone_item* itm, double len, const
		sm::point& pos, double rot, double scale) {
		itm->setPos(0, 0);
		itm->setRotation(0);
		itm->setPolygon(bone_polygon(len, k_node_radius, scale));
		itm->setRotation(ui::radians_to_degrees(rot));
		itm->setPos(ui::to_qt_pt(pos));
	}

}

/*------------------------------------------------------------------------------------------------*/

ui::abstract_canvas_item::abstract_canvas_item() : selection_frame_(nullptr)
{}

void ui::abstract_canvas_item::sync_to_model() {
	sync_item_to_model();
	if (selection_frame_) {
		sync_sel_frame_to_model();
	}
}

ui::canvas* ui::abstract_canvas_item::canvas() const {
	auto* ptr = dynamic_cast<const QGraphicsItem*>(this);
	return static_cast<ui::canvas*>(ptr->scene());
}

bool ui::abstract_canvas_item::is_selected() const {
	return selection_frame_ && selection_frame_->isVisible();
}

void ui::abstract_canvas_item::set_selected(bool selected) {
	if (selected) {
		if (!selection_frame_) {
			selection_frame_ = create_selection_frame();
			selection_frame_->setParentItem(dynamic_cast<QGraphicsItem*>(this));
		}
		selection_frame_->show();
	} else {
		if (selection_frame_) {
			selection_frame_->hide();
		}
	}
}

/*------------------------------------------------------------------------------------------------*/

ui::node_item::node_item(sm::node& node, double scale) :
		has_stick_man_model<ui::node_item, sm::node&>(node),
		is_pinned_(false),
		pin_(nullptr) {
	auto inv_scale = 1.0 / scale;
	setBrush(Qt::white);
	setPen(QPen(Qt::black, 2.0 * inv_scale));
	set_circle(this, to_qt_pt(model_.world_pos()), k_node_radius, inv_scale);
	setZValue(k_node_zorder);
}

void ui::node_item::set_pinned(bool pinned) {
	if (!pin_) {
		pin_ = new QGraphicsEllipseItem();
		set_circle(pin_, { 0,0 }, k_pin_radius, 1.0 / canvas()->scale());
		pin_->setPen(Qt::NoPen);
		pin_->setBrush(Qt::black);
		pin_->setParentItem(this);
	}
	if (pinned) {
		pin_->show();
	} else {
		pin_->hide();
	}
	is_pinned_ = pinned;
}

bool ui::node_item::is_pinned() const {
	return is_pinned_;
}

void ui::node_item::sync_item_to_model() {
	auto& canv = *canvas();
	double inv_scale = 1.0 / canv.scale();
	setPen(QPen(Qt::black, 2.0 * inv_scale));
	set_circle(this, to_qt_pt(model_.world_pos()), k_node_radius, inv_scale);
	if (pin_) {
		set_circle(pin_, { 0,0 }, k_pin_radius, inv_scale);
	}
}

void ui::node_item::sync_sel_frame_to_model() {
	auto* sf = static_cast<QGraphicsEllipseItem*>(selection_frame_);
	auto inv_scale = 1.0 / canvas()->scale();
	set_circle(sf, { 0,0 }, k_node_radius + k_sel_frame_distance, inv_scale);
	sf->setPen(QPen(k_sel_color, k_sel_thickness * inv_scale, Qt::DotLine));
}

QGraphicsItem* ui::node_item::create_selection_frame() const {
	auto& canv = *canvas();
	auto inv_scale = 1.0 / canvas()->scale();
	auto sf = new QGraphicsEllipseItem();
	set_circle(sf, { 0.0,0.0 }, k_node_radius + k_sel_frame_distance, inv_scale);
	sf->setPen(QPen(k_sel_color, k_sel_thickness * inv_scale, Qt::DotLine));
	sf->setBrush(Qt::NoBrush);
	return sf;
}

/*------------------------------------------------------------------------------------------------*/

ui::bone_item::bone_item(sm::bone& bone, double scale) :
		has_stick_man_model<ui::bone_item, sm::bone&>(bone),
		rot_constraint_(nullptr) {
	setBrush(Qt::black);
	setPen(QPen(Qt::black, 1.0 / scale));
	set_bone_item_pos(
		this,
		bone.scaled_length(),
		bone.parent_node().world_pos(),
		bone.world_rotation(),
		1.0 / scale
	);
	setZValue(k_bone_zorder);
}

ui::node_item& ui::bone_item::parent_node_item() const {
	return std::any_cast<std::reference_wrapper<ui::node_item>>(
		model_.parent_node().get_user_data()
	);
}

ui::node_item& ui::bone_item::child_node_item() const {
	return std::any_cast<std::reference_wrapper<ui::node_item>>(
		model_.child_node().get_user_data()
	);
}

void ui::bone_item::sync_rotation_constraint_to_model() {
	auto constraint = model().rotation_constraint();
	if (!constraint) {
		if (rot_constraint_) {
			rot_constraint_->hide();
		}
		return;
	}

	if (!rot_constraint_) {
		canvas()->addItem(rot_constraint_ = new rot_constraint_adornment());
	}
	rot_constraint_->set(model(), *constraint, canvas()->scale());
	if (is_selected()) {
		rot_constraint_->show();
	} else {
		rot_constraint_->hide();
	}
}

void ui::bone_item::sync_item_to_model() {
	auto& canv = *canvas();
	setPen(QPen(Qt::black, 1.0 / canv.scale()));
	set_bone_item_pos(
		this,
		model_.scaled_length(),
		model_.parent_node().world_pos(),
		model_.world_rotation(),
		1.0 / canv.scale()
	);
	sync_rotation_constraint_to_model();
}

void ui::bone_item::sync_sel_frame_to_model() {
	auto* sf = static_cast<QGraphicsLineItem*>(selection_frame_);
	auto inv_scale = 1.0 / canvas()->scale();
	sf->setLine(0, 0, model_.scaled_length(), 0);
	sf->setPen(QPen(k_sel_color, k_sel_thickness * inv_scale, Qt::DotLine));
}

QGraphicsItem* ui::bone_item::create_selection_frame() const {
	auto& canv = *canvas();
	auto inv_scale = 1.0 / canvas()->scale();
	auto sf = new QGraphicsLineItem();
	sf->setLine(0, 0, model_.length(), 0);
	sf->setPen(QPen(k_sel_color, k_sel_thickness * inv_scale, Qt::DotLine));
	return sf;
}

/*------------------------------------------------------------------------------------------------*/


ui::rot_constraint_adornment::rot_constraint_adornment() {
	setBrush(QBrush(k_sel_color));
	setPen(Qt::NoPen);
}

void ui::rot_constraint_adornment::set( const sm::bone& bone,
		const sm::rot_constraint& constraint, double scale) {
	QPointF pivot = {};
	double start_angle = 0;
	double radius = k_joint_constraint_radius * (1.0 / scale);

	if (constraint.relative_to_parent) {
		auto& anchor_bone = bone.parent_bone()->get();
		auto parent_rot = anchor_bone.world_rotation();
		
		start_angle = normalize_angle(parent_rot + constraint.start_angle);
		pivot = ui::to_qt_pt(bone.parent_node().world_pos());
	} else {
		auto center_pt = 0.5 * (bone.parent_node().world_pos() + bone.child_node().world_pos());
		start_angle = constraint.start_angle;
		pivot = ui::to_qt_pt(center_pt);
	}
	ui::set_arc(this, pivot, radius, start_angle, constraint.span_angle);
	show();
}
