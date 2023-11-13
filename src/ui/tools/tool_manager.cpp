#include "tool_manager.h"
#include "../canvas/canvas_manager.h"
#include "tool.h"
#include "selection_tool.h"
#include "pan_tool.h"
#include "zoom_tool.h"
#include "move_tool.h"
#include "add_node_tool.h"
#include "add_bone_tool.h"
#include <ranges>

namespace r = std::ranges;
namespace rv = std::ranges::views;

/*------------------------------------------------------------------------------------------------*/

ui::tool::manager::manager() :
    curr_item_index_(-1) {
    tool_registry_.emplace_back(std::make_unique<ui::tool::pan_tool>());
    tool_registry_.emplace_back(std::make_unique<ui::tool::zoom_tool>());
    tool_registry_.emplace_back(std::make_unique<ui::tool::selection_tool>());
    tool_registry_.emplace_back(std::make_unique<ui::tool::move_tool>());
    tool_registry_.emplace_back(std::make_unique<ui::tool::add_node_tool>());
    tool_registry_.emplace_back(std::make_unique<ui::tool::add_bone_tool>());
}

void ui::tool::manager::init(canvas::manager& canvases, mdl::project& model) {
    for (auto& tool : tool_registry_) {
        tool->init(canvases, model);
    }
}

void ui::tool::manager::keyPressEvent(ui::canvas::scene& c, QKeyEvent* event) {
    if (has_current_tool()) {
        current_tool().keyPressEvent(c, event);
    }
}

void ui::tool::manager::keyReleaseEvent(ui::canvas::scene& c, QKeyEvent* event) {
    if (has_current_tool()) {
        current_tool().keyReleaseEvent(c, event);
    }
}

void ui::tool::manager::mousePressEvent(ui::canvas::scene& c, QGraphicsSceneMouseEvent* event) {
    if (has_current_tool()) {
        current_tool().mousePressEvent(c, event);
    }
}

void ui::tool::manager::mouseMoveEvent(ui::canvas::scene& c, QGraphicsSceneMouseEvent* event) {
    if (has_current_tool()) {
        current_tool().mouseMoveEvent(c, event);
    }
}

void ui::tool::manager::mouseReleaseEvent(ui::canvas::scene& c, QGraphicsSceneMouseEvent* event) {
    if (has_current_tool()) {
        current_tool().mouseReleaseEvent(c, event);
    }
}

void ui::tool::manager::mouseDoubleClickEvent(ui::canvas::scene& c, QGraphicsSceneMouseEvent* event) {
    if (has_current_tool()) {
        current_tool().mouseDoubleClickEvent(c, event);
    }
}

void ui::tool::manager::wheelEvent(ui::canvas::scene& c, QGraphicsSceneWheelEvent* event) {
    if (has_current_tool()) {
        current_tool().wheelEvent(c, event);
    }
}

std::span<const ui::tool::fields> ui::tool::manager::tool_info() const {
    static std::vector<ui::tool::fields> tool_records;
    if (tool_records.empty()) {
        tool_records = tool_registry_ |
            rv::transform(
                [](const auto& t)->ui::tool::fields {
                    return ui::tool::fields{
                        t->id(), t->name(), t->icon_rsrc()
                    };
                }
        ) | r::to<std::vector<ui::tool::fields>>();
    }
    return tool_records;
}

bool ui::tool::manager::has_current_tool() const {
    return curr_item_index_ >= 0;
}

ui::tool::base& ui::tool::manager::current_tool() const {
    return *tool_registry_.at(curr_item_index_);
}

void ui::tool::manager::set_current_tool(canvas::manager& canvases, id id) {
    int new_tool_index = index_from_id(id);
    if (new_tool_index == curr_item_index_) {
        return;
    }
    canvases.active_canvas().setFocus();
    if (has_current_tool()) {
        current_tool().deactivate(canvases);
    }
    curr_item_index_ = new_tool_index;
    current_tool().activate(canvases);

    emit current_tool_changed(current_tool());
}

int ui::tool::manager::index_from_id(id id) const {
    auto iter = r::find_if(tool_registry_, [id](const auto& t) {return id == t->id(); });
    return std::distance(tool_registry_.begin(), iter);
}