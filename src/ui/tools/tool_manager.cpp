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

ui::tool::tool_manager::tool_manager() :
    curr_item_index_(-1) {
    tool_registry_.emplace_back(std::make_unique<ui::tool::pan_tool>());
    tool_registry_.emplace_back(std::make_unique<ui::tool::zoom_tool>());
    tool_registry_.emplace_back(std::make_unique<ui::tool::selection_tool>());
    tool_registry_.emplace_back(std::make_unique<ui::tool::move_tool>());
    tool_registry_.emplace_back(std::make_unique<ui::tool::add_node_tool>());
    tool_registry_.emplace_back(std::make_unique<ui::tool::add_bone_tool>());
}

void ui::tool::tool_manager::init(canvas::manager& canvases, mdl::project& model) {
    for (auto& tool : tool_registry_) {
        tool->init(canvases, model);
    }
}

void ui::tool::tool_manager::keyPressEvent(ui::canvas::scene& c, QKeyEvent* event) {
    if (has_current_tool()) {
        current_tool().keyPressEvent(c, event);
    }
}

void ui::tool::tool_manager::keyReleaseEvent(ui::canvas::scene& c, QKeyEvent* event) {
    if (has_current_tool()) {
        current_tool().keyReleaseEvent(c, event);
    }
}

void ui::tool::tool_manager::mousePressEvent(ui::canvas::scene& c, QGraphicsSceneMouseEvent* event) {
    if (has_current_tool()) {
        current_tool().mousePressEvent(c, event);
    }
}

void ui::tool::tool_manager::mouseMoveEvent(ui::canvas::scene& c, QGraphicsSceneMouseEvent* event) {
    if (has_current_tool()) {
        current_tool().mouseMoveEvent(c, event);
    }
}

void ui::tool::tool_manager::mouseReleaseEvent(ui::canvas::scene& c, QGraphicsSceneMouseEvent* event) {
    if (has_current_tool()) {
        current_tool().mouseReleaseEvent(c, event);
    }
}

void ui::tool::tool_manager::mouseDoubleClickEvent(ui::canvas::scene& c, QGraphicsSceneMouseEvent* event) {
    if (has_current_tool()) {
        current_tool().mouseDoubleClickEvent(c, event);
    }
}

void ui::tool::tool_manager::wheelEvent(ui::canvas::scene& c, QGraphicsSceneWheelEvent* event) {
    if (has_current_tool()) {
        current_tool().wheelEvent(c, event);
    }
}

std::span<const ui::tool::tool_info> ui::tool::tool_manager::tool_info() const {
    static std::vector<ui::tool::tool_info> tool_records;
    if (tool_records.empty()) {
        tool_records = tool_registry_ |
            rv::transform(
                [](const auto& t)->ui::tool::tool_info {
                    return ui::tool::tool_info{
                        t->id(), t->name(), t->icon_rsrc()
                    };
                }
        ) | r::to<std::vector<ui::tool::tool_info>>();
    }
    return tool_records;
}

bool ui::tool::tool_manager::has_current_tool() const {
    return curr_item_index_ >= 0;
}

ui::tool::base& ui::tool::tool_manager::current_tool() const {
    return *tool_registry_.at(curr_item_index_);
}

void ui::tool::tool_manager::set_current_tool(canvas::manager& canvases, tool_id id) {
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

int ui::tool::tool_manager::index_from_id(tool_id id) const {
    auto iter = r::find_if(tool_registry_, [id](const auto& t) {return id == t->id(); });
    return std::distance(tool_registry_.begin(), iter);
}