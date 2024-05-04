#include "animate_tool.h"
#include "../canvas/node_item.h"
#include "../canvas/bone_item.h"
#include "../canvas/skel_item.h"
#include "../panes/tool_settings_pane.h"
#include "../util.h"
#include "../../core/sm_skeleton.h"
#include "../../core/sm_fabrik.h"
#include "../../core/sm_visit.h"
#include <unordered_map>
#include <array>
#include <numbers>

/*------------------------------------------------------------------------------------------------*/


/*------------------------------------------------------------------------------------------------*/

ui::tool::animate::animate() :
        base("animate", "move_icon.png", ui::tool::id::animate) {
}

void ui::tool::animate::keyPressEvent(canvas::scene& c, QKeyEvent* event) {
}

void ui::tool::animate::mousePressEvent(canvas::scene& c, QGraphicsSceneMouseEvent* event) {
}

void ui::tool::animate::mouseMoveEvent(canvas::scene& c, QGraphicsSceneMouseEvent* event) {
}

void ui::tool::animate::mouseReleaseEvent(canvas::scene& c, QGraphicsSceneMouseEvent* event) {
}

QWidget* ui::tool::animate::settings_widget() {
    return nullptr;
}
