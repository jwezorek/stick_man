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

ui::tool::base::base(QString name, QString rsrc, tool::id id) :
    name_(name),
    rsrc_(rsrc),
    id_(id) {
}

ui::tool::id ui::tool::base::id() const {
    return id_;
}

QString ui::tool::base::name() const {
    return name_;
}

QString ui::tool::base::icon_rsrc() const {
    return rsrc_;
}

void ui::tool::base::populate_settings(pane::tool_settings* pane) {
    pane->set_tool(name_, settings_widget());
}

void ui::tool::base::activate(canvas::manager& canvases) {}
void ui::tool::base::keyPressEvent(canvas::scene& c, QKeyEvent* event) {}
void ui::tool::base::keyReleaseEvent(canvas::scene& c, QKeyEvent* event) {}
void ui::tool::base::mousePressEvent(canvas::scene& c, QGraphicsSceneMouseEvent* event) {}
void ui::tool::base::mouseMoveEvent(canvas::scene& c, QGraphicsSceneMouseEvent* event) {}
void ui::tool::base::mouseReleaseEvent(canvas::scene& c, QGraphicsSceneMouseEvent* event) {}
void ui::tool::base::mouseDoubleClickEvent(canvas::scene& c, QGraphicsSceneMouseEvent* event) {}
void ui::tool::base::wheelEvent(canvas::scene& c, QGraphicsSceneWheelEvent* event) {}
void ui::tool::base::deactivate(canvas::manager& canvases) {}
void ui::tool::base::init(canvas::manager&, mdl::project&) {}
QWidget* ui::tool::base::settings_widget() { return nullptr; }
ui::tool::base::~base() {}


