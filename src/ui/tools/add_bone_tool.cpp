#include "add_bone_tool.h"
#include "../canvas/node_item.h"

/*------------------------------------------------------------------------------------------------*/

void ui::tool::add_bone::init_rubber_band(canvas::scene& c) {
    if (!rubber_band_) {
        c.addItem(rubber_band_ = new QGraphicsLineItem());
        rubber_band_->setPen(QPen(Qt::black, 2.0, Qt::DotLine));
        return;
    }
    else if (rubber_band_->scene() != &c) {
        rubber_band_->scene()->removeItem(rubber_band_);
        c.addItem(rubber_band_);
    }
}

ui::tool::add_bone::add_bone() :
    model_(nullptr),
    rubber_band_(nullptr),
    base("add bone", "add_bone_icon.png", ui::tool::id::add_bone) {
}

void ui::tool::add_bone::init(canvas::manager& canvases, mdl::project& model) {
    model_ = &model;
}

void ui::tool::add_bone::mousePressEvent(canvas::scene& c, QGraphicsSceneMouseEvent* event) {
    origin_ = event->scenePos();
    init_rubber_band(c);
    rubber_band_->setLine(QLineF(origin_, origin_));
    rubber_band_->show();
}

void ui::tool::add_bone::mouseMoveEvent(canvas::scene& c, QGraphicsSceneMouseEvent* event) {
    rubber_band_->setLine(QLineF(origin_, event->scenePos()));
}

void ui::tool::add_bone::mouseReleaseEvent(canvas::scene& canv, QGraphicsSceneMouseEvent* event) {
    rubber_band_->hide();
    auto pt = event->scenePos();
    auto parent_node = canv.top_node(origin_);
    auto child_node = canv.top_node(event->scenePos());

    if (!parent_node || !child_node || parent_node == child_node) {
        return;
    }

    model_->add_bone(
        canv.tab_name(),
        mdl::to_handle(parent_node->model()),
        mdl::to_handle(child_node->model())
    );
}