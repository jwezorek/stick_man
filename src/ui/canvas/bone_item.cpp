#include "scene.h"
#include "canvas_item.h"
#include "bone_item.h"
#include "../util.h"
#include "../../core/sm_skeleton.h"

/*------------------------------------------------------------------------------------------------*/

namespace {

    constexpr auto k_joint_constraint_radius = 50.0;
    constexpr auto k_bone_zorder = 5;

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

    void set_bone_item_pos( ui::canvas::item::bone_item* itm, double len, const
            sm::point& pos, double rot, double scale) {
        itm->setPos(0, 0);
        itm->setRotation(0);
        itm->setPolygon(bone_polygon(len, ui::canvas::k_node_radius, scale));
        itm->setRotation(ui::radians_to_degrees(rot));
        itm->setPos(ui::to_qt_pt(pos));
    }

}

/*------------------------------------------------------------------------------------------------*/

ui::canvas::item::bone_item::bone_item(sm::bone& bone, double scale) :
        treeview_item_(nullptr),
        has_stick_man_model<ui::canvas::item::bone_item, sm::bone&>(bone),
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

ui::canvas::item::node& ui::canvas::item::bone_item::parent_node_item() const {
    return std::any_cast<std::reference_wrapper<ui::canvas::item::node>>(
        model_.parent_node().get_user_data()
    );
}

ui::canvas::item::node& ui::canvas::item::bone_item::child_node_item() const {
    return std::any_cast<std::reference_wrapper<ui::canvas::item::node>>(
        model_.child_node().get_user_data()
    );
}

void ui::canvas::item::bone_item::sync_rotation_constraint_to_model() {
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
    }
    else {
        rot_constraint_->hide();
    }
}

mdl::const_skel_piece ui::canvas::item::bone_item::to_skeleton_piece() const {
    const auto& bone = model();
    return std::ref(bone);
}

void ui::canvas::item::bone_item::sync_item_to_model() {
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

void ui::canvas::item::bone_item::sync_sel_frame_to_model() {
    auto* sf = static_cast<QGraphicsLineItem*>(selection_frame_);
    auto inv_scale = 1.0 / canvas()->scale();
    sf->setLine(0, 0, model_.scaled_length(), 0);
    sf->setPen(QPen(k_sel_color, k_sel_thickness * inv_scale, Qt::DotLine));
}

QGraphicsItem* ui::canvas::item::bone_item::create_selection_frame() const {
    auto& canv = *canvas();
    auto inv_scale = 1.0 / canvas()->scale();
    auto sf = new QGraphicsLineItem();
    sf->setLine(0, 0, model_.length(), 0);
    sf->setPen(QPen(k_sel_color, k_sel_thickness * inv_scale, Qt::DotLine));
    return sf;
}

bool ui::canvas::item::bone_item::is_selection_frame_only() const {
    return false;
}

QGraphicsItem* ui::canvas::item::bone_item::item_body() {
    return this;
}

/*------------------------------------------------------------------------------------------------*/


ui::canvas::item::rot_constraint_adornment::rot_constraint_adornment() {
    setBrush(QBrush(k_sel_color));
    setPen(Qt::NoPen);
}

void ui::canvas::item::rot_constraint_adornment::set(const sm::bone& bone,
    const sm::rot_constraint& constraint, double scale) {
    QPointF pivot = {};
    double start_angle = 0;
    double radius = k_joint_constraint_radius * (1.0 / scale);

    if (constraint.relative_to_parent) {
        auto& anchor_bone = bone.parent_bone()->get();
        auto parent_rot = anchor_bone.world_rotation();

        start_angle = normalize_angle(parent_rot + constraint.start_angle);
        pivot = ui::to_qt_pt(bone.parent_node().world_pos());
    }
    else {
        auto center_pt = 0.5 * (bone.parent_node().world_pos() + bone.child_node().world_pos());
        start_angle = constraint.start_angle;
        pivot = ui::to_qt_pt(center_pt);
    }
    ui::set_arc(this, pivot, radius, start_angle, constraint.span_angle);
    show();
}