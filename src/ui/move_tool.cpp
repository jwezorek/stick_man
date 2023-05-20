#include "move_tool.h"
#include "tool_settings_pane.h"

/*------------------------------------------------------------------------------------------------*/

namespace {

    enum class move_mode {
        none = -1,
        translate = 0,
        rotate,
        pinned_ik,
        ik_free
    };

    auto k_move_mode = std::to_array<std::tuple< move_mode, QString>>({
        { move_mode::translate , "translate (free)"},
        { move_mode::rotate , "rotate (forward kinematics)" },
        { move_mode::pinned_ik , "translate pinned (ik)" },
        { move_mode::ik_free , "translate unpinned (ik)" }
        });

    void rotate_mouse_press(ui::canvas& c, QGraphicsSceneMouseEvent* event) {
    }

    void rotate_mouse_move(ui::canvas& c, QGraphicsSceneMouseEvent* event) {
    }

    void rotate_mouse_release(ui::canvas& c, QGraphicsSceneMouseEvent* event) {
    }
}

ui::move_tool::move_tool() : abstract_tool("move", "move_icon.png", ui::tool_id::move) {

}

void ui::move_tool::mousePressEvent(canvas& c, QGraphicsSceneMouseEvent* event) {
    auto mode = static_cast<move_mode>(btns_->checkedId());
    switch (mode) {
    case move_mode::rotate:
        rotate_mouse_press(c, event);
        return;
    default:
        return;
    }
}

void ui::move_tool::mouseMoveEvent(canvas& c, QGraphicsSceneMouseEvent* event) {
    auto mode = static_cast<move_mode>(btns_->checkedId());
    switch (mode) {
    case move_mode::rotate:
        rotate_mouse_move(c, event);
        return;
    default:
        return;
    }
}

void ui::move_tool::mouseReleaseEvent(canvas& c, QGraphicsSceneMouseEvent* event) {
    auto mode = static_cast<move_mode>(btns_->checkedId());
    switch (mode) {
    case move_mode::rotate:
        rotate_mouse_release(c, event);
        return;
    default:
        return;
    }
}

void ui::move_tool::populate_settings_aux(tool_settings_pane* pane) {
    btns_ = std::make_unique<QButtonGroup>();
    for (const auto& [id, name] : k_move_mode) {
        auto* btn = new QRadioButton(name);
        btns_->addButton(btn, static_cast<int>(id));
        pane->contents()->addWidget(btn);
    }
}
