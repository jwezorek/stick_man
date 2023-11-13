#include "add_bone_tool.h"
#include "../canvas/node_item.h"

/*------------------------------------------------------------------------------------------------*/

void ui::add_bone_tool::init_rubber_band(canvas::canvas& c) {
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

ui::add_bone_tool::add_bone_tool() :
    model_(nullptr),
    rubber_band_(nullptr),
    tool("add bone", "add_bone_icon.png", ui::tool_id::add_bone) {
}

void ui::add_bone_tool::init(canvas::canvas_manager& canvases, mdl::project& model) {
    model_ = &model;
}

void ui::add_bone_tool::mousePressEvent(canvas::canvas& c, QGraphicsSceneMouseEvent* event) {
    origin_ = event->scenePos();
    init_rubber_band(c);
    rubber_band_->setLine(QLineF(origin_, origin_));
    rubber_band_->show();
}

void ui::add_bone_tool::mouseMoveEvent(canvas::canvas& c, QGraphicsSceneMouseEvent* event) {
    rubber_band_->setLine(QLineF(origin_, event->scenePos()));
}

void ui::add_bone_tool::mouseReleaseEvent(canvas::canvas& canv, QGraphicsSceneMouseEvent* event) {
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