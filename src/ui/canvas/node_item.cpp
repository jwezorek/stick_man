#include "canvas.h"
#include "canvas_item.h"
#include "node_item.h"
#include "../util.h"
#include "../../core/sm_skeleton.h"

/*------------------------------------------------------------------------------------------------*/

namespace r = std::ranges;
namespace rv = std::ranges::views;

namespace {

    constexpr auto k_node_zorder = 10;
    constexpr auto k_pin_radius = ui::canvas::k_node_radius - 3.0;

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

}

/*------------------------------------------------------------------------------------------------*/

ui::canvas::node_item::node_item(sm::node& node, double scale) :
    has_stick_man_model<ui::canvas::node_item, sm::node&>(node),
    is_pinned_(false),
    pin_(nullptr) {
    auto inv_scale = 1.0 / scale;
    setBrush(Qt::white);
    setPen(QPen(Qt::black, 2.0 * inv_scale));
    set_circle(this, to_qt_pt(model_.world_pos()), k_node_radius, inv_scale);
    setZValue(k_node_zorder);
}

void ui::canvas::node_item::set_pinned(bool pinned) {
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

bool ui::canvas::node_item::is_pinned() const {
    return is_pinned_;
}

void ui::canvas::node_item::sync_item_to_model() {
    auto& canv = *canvas();
    double inv_scale = 1.0 / canv.scale();
    setPen(QPen(Qt::black, 2.0 * inv_scale));
    set_circle(this, to_qt_pt(model_.world_pos()), k_node_radius, inv_scale);
    if (pin_) {
        set_circle(pin_, { 0,0 }, k_pin_radius, inv_scale);
    }
}

void ui::canvas::node_item::sync_sel_frame_to_model() {
    auto* sf = static_cast<QGraphicsEllipseItem*>(selection_frame_);
    auto inv_scale = 1.0 / canvas()->scale();
    set_circle(sf, { 0,0 }, k_node_radius + k_sel_frame_distance, inv_scale);
    sf->setPen(QPen(k_sel_color, k_sel_thickness * inv_scale, Qt::DotLine));
}

QGraphicsItem* ui::canvas::node_item::create_selection_frame() const {
    auto& canv = *canvas();
    auto inv_scale = 1.0 / canvas()->scale();
    auto sf = new QGraphicsEllipseItem();
    set_circle(sf, { 0.0,0.0 }, k_node_radius + k_sel_frame_distance, inv_scale);
    sf->setPen(QPen(k_sel_color, k_sel_thickness * inv_scale, Qt::DotLine));
    sf->setBrush(Qt::NoBrush);
    return sf;
}

bool ui::canvas::node_item::is_selection_frame_only() const {
    return false;
}

QGraphicsItem* ui::canvas::node_item::item_body() {
    return this;
}

mdl::const_skel_piece ui::canvas::node_item::to_skeleton_piece() const {
    auto& node = model();
    return std::ref(node);
}