#include "add_node_tool.h"

/*------------------------------------------------------------------------------------------------*/

ui::add_node_tool::add_node_tool() :
    model_(nullptr),
    tool("add node", "add_node_icon.png", ui::tool_id::add_node)
{}

void ui::add_node_tool::init(canvas::manager& canvases, mdl::project& model) {
    model_ = &model;
}

void ui::add_node_tool::mouseReleaseEvent(canvas::scene& canv, QGraphicsSceneMouseEvent* event) {
    model_->add_new_skeleton_root(canv.tab_name(), from_qt_pt(event->scenePos()));
}
