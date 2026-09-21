#include "tool_manager.hpp"
#include "../canvas/canvas_manager.hpp"
#include "tool.hpp"
#include "selection_tool.hpp"
#include "animate_tool.hpp"
#include "pan_tool.hpp"
#include "zoom_tool.hpp"
#include "add_node_tool.hpp"
#include "add_bone_tool.hpp"
#include "constraint_tool.hpp"
#include <ranges>

namespace r = std::ranges;
namespace rv = std::ranges::views;

/*------------------------------------------------------------------------------------------------*/

ui::tool::manager::manager() :
    curr_item_index_(-1) {
    tool_registry_.emplace_back(std::make_unique<ui::tool::pan>());
    tool_registry_.emplace_back(std::make_unique<ui::tool::zoom>());
    tool_registry_.emplace_back(std::make_unique<ui::tool::select>());
    tool_registry_.emplace_back(std::make_unique<ui::tool::animate>());
    tool_registry_.emplace_back(std::make_unique<ui::tool::constraint>());
    tool_registry_.emplace_back(std::make_unique<ui::tool::add_node>());
    tool_registry_.emplace_back(std::make_unique<ui::tool::add_bone>());
}

void ui::tool::manager::init(canvas::manager& canvases, mdl::project& model) {
    project_ = &model;
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
        // Animate is the Animation Mode counterpart of Selection.  It is a real
        // tool internally, but deliberately shares Selection's toolbar button.
        tool_records = tool_registry_ |
            rv::filter([](const auto& t) { return t->id() != id::animate; }) |
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

const ui::tool::base& ui::tool::manager::tool_from_id(id id) const {
    for (const auto& reg : tool_registry_) {
        if (reg->id() == id) {
            return *reg;
        }
    }
    throw std::runtime_error("tool not found");
}

void ui::tool::manager::set_current_tool(canvas::manager& canvases, id id) {
    if (project_ && project_->animation_mode()) {
        // The Selection toolbar slot becomes the real Animate tool while in
        // Animation Mode.  Pan/Zoom/Constraint remain independently usable.
        if (id == id::selection) id = id::animate;
        if (id != id::animate && id != id::pan && id != id::zoom && id != id::constraint) {
            return;
        }
    }
    else if (id == id::animate) {
        // Animate is not directly user-selectable outside Animation Mode.
        id = id::selection;
    }
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

ui::tool::base& ui::tool::manager::tool_from_id(id id) {
    return const_cast<base&>(std::as_const(*this).tool_from_id(id));
}
