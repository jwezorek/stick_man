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

ui::tool::tool(QString name, QString rsrc, tool_id id) :
    name_(name),
    rsrc_(rsrc),
    id_(id) {
}

ui::tool_id ui::tool::id() const {
    return id_;
}

QString ui::tool::name() const {
    return name_;
}

QString ui::tool::icon_rsrc() const {
    return rsrc_;
}

void ui::tool::populate_settings(pane::tool_settings* pane) {
    pane->set_tool(name_, settings_widget());
}

void ui::tool::activate(canvas_manager& canvases) {}
void ui::tool::keyPressEvent(canvas& c, QKeyEvent* event) {}
void ui::tool::keyReleaseEvent(canvas& c, QKeyEvent* event) {}
void ui::tool::mousePressEvent(canvas& c, QGraphicsSceneMouseEvent* event) {}
void ui::tool::mouseMoveEvent(canvas& c, QGraphicsSceneMouseEvent* event) {}
void ui::tool::mouseReleaseEvent(canvas& c, QGraphicsSceneMouseEvent* event) {}
void ui::tool::mouseDoubleClickEvent(canvas& c, QGraphicsSceneMouseEvent* event) {}
void ui::tool::wheelEvent(canvas& c, QGraphicsSceneWheelEvent* event) {}
void ui::tool::deactivate(canvas_manager& canvases) {}
void ui::tool::init(canvas_manager&, mdl::project&) {}
QWidget* ui::tool::settings_widget() { return nullptr; }
ui::tool::~tool() {}


