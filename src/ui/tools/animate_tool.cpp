#include "animate_tool.hpp"
#include "../canvas/node_item.hpp"
#include "../canvas/bone_item.hpp"
#include "../canvas/skel_item.hpp"
#include "../panes/tool_settings_pane.hpp"
#include "../util.hpp"
#include "../../core/sm_skeleton.hpp"
#include "../../core/sm_fabrik.hpp"
#include "../../core/sm_visit.hpp"
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
