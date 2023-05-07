#include "tools.h"
#include "stick_man.h"
#include <array>
#include <functional>
#include <ranges>

namespace r = std::ranges;
namespace rv = std::ranges::views;

/*------------------------------------------------------------------------------------------------*/

namespace {
    class placeholder : public ui::abstract_tool {
    public:
        placeholder(QString name, QString rsrc, ui::tool_id id) : abstract_tool(name, rsrc, id) {}
        void handle_key_press(ui::canvas*, QKeyEvent* ) override {}
        void handle_mouse_press(ui::canvas*, QMouseEvent* ) override {}
        void handle_mouse_move(ui::canvas*, QMouseEvent* ) override {}
        void handle_mouse_release(ui::canvas*, QMouseEvent* ) override {}
    };

    auto k_tool_registry = std::to_array<std::unique_ptr<ui::abstract_tool>>({
        std::make_unique<ui::pan_tool>(),
        std::make_unique<ui::zoom_tool>(),
        std::make_unique<placeholder>("arrow tool", "arrow_icon.png", ui::tool_id::arrow),
        std::make_unique<placeholder>("move tool", "move_icon.png", ui::tool_id::move),
        std::make_unique<placeholder>("add joint tool", "add_joint_icon.png", ui::tool_id::add_joint),
        std::make_unique<placeholder>("add bone tool", "add_bone_icon.png", ui::tool_id::add_bone) 
    });

}

/*------------------------------------------------------------------------------------------------*/

ui::abstract_tool::abstract_tool(QString name, QString rsrc, tool_id id) : 
    name_(name),
    rsrc_(rsrc),
    id_(id) {

}

ui::tool_id ui::abstract_tool::id() const {
    return id_;
}

QString ui::abstract_tool::name() const {
    return name_;
}

QString ui::abstract_tool::icon_rsrc() const {
    return rsrc_;
}

/*------------------------------------------------------------------------------------------------*/

ui::zoom_tool::zoom_tool() : abstract_tool("zoom", "zoom_icon.png", ui::tool_id::zoom) {
}

void ui::zoom_tool::handle_key_press(canvas* c, QKeyEvent* event) {

}

void ui::zoom_tool::handle_mouse_press(canvas* c, QMouseEvent* event) {

}

void ui::zoom_tool::handle_mouse_move(canvas* c, QMouseEvent* event) {

}

void ui::zoom_tool::handle_mouse_release(canvas* c, QMouseEvent* event) {

}

/*------------------------------------------------------------------------------------------------*/
 
ui::pan_tool::pan_tool() : abstract_tool("pan", "pan_icon.png", ui::tool_id::pan) {
}

void ui::pan_tool::handle_mouse_press(canvas* c, QMouseEvent* event) {

}

void ui::pan_tool::handle_key_press(canvas* c, QKeyEvent* event) {

}

void ui::pan_tool::handle_mouse_move(canvas* c, QMouseEvent* event) {

}

void ui::pan_tool::handle_mouse_release(canvas* c, QMouseEvent* event) {

}

/*------------------------------------------------------------------------------------------------*/

ui::tool_manager::tool_manager(stick_man* sm) :
        main_window_(*sm),
        curr_item_index_(-1){
}

std::span<const ui::tool_info> ui::tool_manager::tool_info() const {
    static std::vector<ui::tool_info> tool_records;
    if (tool_records.empty()) {
        tool_records = k_tool_registry |
            rv::transform(
                [](const auto& t)->ui::tool_info {
                    return ui::tool_info{
                        t->id(), t->name(), t->icon_rsrc()
                    };
                }
            ) | r::to<std::vector<ui::tool_info>>();
    }
    return tool_records;
}

void ui::tool_manager::handle_key_press(QKeyEvent* event) {

}

void ui::tool_manager::handle_mouse_press(QMouseEvent* event) {

}

void ui::tool_manager::handle_mouse_move(QMouseEvent* event) {

}

void ui::tool_manager::handle_mouse_release(QMouseEvent* event) {

}

bool ui::tool_manager::has_current_tool() const {
    return curr_item_index_ >= 0;
}

ui::abstract_tool& ui::tool_manager::current_tool() const {
    return *k_tool_registry.at(curr_item_index_);
}

void ui::tool_manager::set_current_tool(tool_id id) {
    auto iter = r::find_if(k_tool_registry, [id](const auto& t) {return id == t->id(); });
    curr_item_index_ = std::distance(k_tool_registry.begin(), iter);
}
