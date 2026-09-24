#include "scene.hpp"
#include "canvas_item.hpp"
#include "bone_item.hpp"
#include "../util.hpp"
#include "../../core/sm_skeleton.hpp"

/*------------------------------------------------------------------------------------------------*/

namespace {

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

    void set_bone_item_pos( ui::canvas::item::bone* itm, double len, const
            sm::point& pos, double rot, double scale) {
        itm->setPos(0, 0);
        itm->setRotation(0);
        itm->setPolygon(bone_polygon(len, ui::canvas::k_node_radius, scale));
        itm->setRotation(ui::radians_to_degrees(rot));
        itm->setPos(ui::to_qt_pt(pos));
    }

}

/*------------------------------------------------------------------------------------------------*/

ui::canvas::item::bone::bone(sm::bone& bone, double scale) :
        treeview_item_(nullptr),
        has_stick_man_model<ui::canvas::item::bone, sm::bone&>(bone) {
    apply_display_style(scale);
    set_bone_item_pos(
        this,
        bone.scaled_length(),
        bone.parent_node().world_pos(),
        bone.world_rotation(),
        1.0 / scale
    );
    setZValue(k_bone_zorder);
}

ui::canvas::item::node& ui::canvas::item::bone::parent_node_item() const {
    return std::any_cast<sm::ref<ui::canvas::item::node>>(
        model_.parent_node().get_user_data()
    );
}

ui::canvas::item::node& ui::canvas::item::bone::child_node_item() const {
    return std::any_cast<sm::ref<ui::canvas::item::node>>(
        model_.child_node().get_user_data()
    );
}

mdl::const_skel_piece ui::canvas::item::bone::to_skeleton_piece() const {
    const auto& bone = model();
    return sm::ref(bone);
}

void ui::canvas::item::bone::apply_display_style(double scale) {
    if (wireframe_) {
        setBrush(Qt::NoBrush);
        setPen(QPen(Qt::black, 2.0 / scale, Qt::DotLine));
    } else {
        setBrush(Qt::black);
        setPen(QPen(Qt::black, 1.0 / scale));
    }
}

void ui::canvas::item::bone::set_wireframe(bool wireframe) {
    wireframe_ = wireframe;
    apply_display_style(canvas() ? canvas()->scale() : 1.0);
    update();
}

void ui::canvas::item::bone::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) {
    if (!wireframe_) {
        QGraphicsPolygonItem::paint(painter, option, widget);
        return;
    }
    painter->save();
    painter->setPen(pen());
    painter->setBrush(Qt::NoBrush);
    painter->drawLine(QPointF(0.0, 0.0), QPointF(model_.scaled_length(), 0.0));
    painter->restore();
}

void ui::canvas::item::bone::sync_item_to_model() {
    auto& canv = *canvas();
    apply_display_style(canv.scale());
    set_bone_item_pos(
        this,
        model_.scaled_length(),
        model_.parent_node().world_pos(),
        model_.world_rotation(),
        1.0 / canv.scale()
    );
}

void ui::canvas::item::bone::sync_sel_frame_to_model() {
    auto* sf = static_cast<QGraphicsLineItem*>(selection_frame_);
    auto inv_scale = 1.0 / canvas()->scale();
    sf->setLine(0, 0, model_.scaled_length(), 0);
    sf->setPen(QPen(k_sel_color, k_sel_thickness * inv_scale, Qt::DotLine));
}

QGraphicsItem* ui::canvas::item::bone::create_selection_frame() const {
    auto& canv = *canvas();
    auto inv_scale = 1.0 / canvas()->scale();
    auto sf = new QGraphicsLineItem();
    sf->setLine(0, 0, model_.length(), 0);
    sf->setPen(QPen(k_sel_color, k_sel_thickness * inv_scale, Qt::DotLine));
    return sf;
}

bool ui::canvas::item::bone::is_selection_frame_only() const {
    return false;
}

QGraphicsItem* ui::canvas::item::bone::item_body() {
    return this;
}
