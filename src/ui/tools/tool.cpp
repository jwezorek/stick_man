#include "tool.h"
#include "move_tool.h"
#include "selection_tool.h"
#include "../stick_man.h"
#include "../panes/tool_settings_pane.h"
#include "../util.h"
#include "../stick_man.h"
#include "../canvas/node_item.h"
#include "../canvas/bone_item.h"
#include "../canvas/skel_item.h"
#include "../canvas/canvas_manager.h"
#include "../../model/project.h"
#include "../../model/handle.h"
#include <QGraphicsScene>
#include <array>
#include <functional>
#include <ranges>

namespace r = std::ranges;
namespace rv = std::ranges::views;

/*------------------------------------------------------------------------------------------------*/

ui::tool::tool::tool(QString name, QString rsrc, tool_id id) :
    name_(name),
    rsrc_(rsrc),
    id_(id) {
}

ui::tool::tool_id ui::tool::tool::id() const {
    return id_;
}

QString ui::tool::tool::name() const {
    return name_;
}

QString ui::tool::tool::icon_rsrc() const {
    return rsrc_;
}

void ui::tool::tool::populate_settings(pane::tool_settings* pane) {
    pane->set_tool(name_, settings_widget());
}

void ui::tool::tool::activate(canvas::manager& canvases) {}
void ui::tool::tool::keyPressEvent(canvas::scene& c, QKeyEvent* event) {}
void ui::tool::tool::keyReleaseEvent(canvas::scene& c, QKeyEvent* event) {}
void ui::tool::tool::mousePressEvent(canvas::scene& c, QGraphicsSceneMouseEvent* event) {}
void ui::tool::tool::mouseMoveEvent(canvas::scene& c, QGraphicsSceneMouseEvent* event) {}
void ui::tool::tool::mouseReleaseEvent(canvas::scene& c, QGraphicsSceneMouseEvent* event) {}
void ui::tool::tool::mouseDoubleClickEvent(canvas::scene& c, QGraphicsSceneMouseEvent* event) {}
void ui::tool::tool::wheelEvent(canvas::scene& c, QGraphicsSceneWheelEvent* event) {}
void ui::tool::tool::deactivate(canvas::manager& canvases) {}
void ui::tool::tool::init(canvas::manager&, mdl::project&) {}
QWidget* ui::tool::tool::settings_widget() { return nullptr; }
ui::tool::tool::~tool() {}


