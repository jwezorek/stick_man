#include "pan_tool.hpp"
#include "../canvas/canvas_manager.hpp"

/*------------------------------------------------------------------------------------------------*/

ui::tool::pan::pan() :
    base("pan", "pan_icon.png", ui::tool::id::pan)
{}

void ui::tool::pan::deactivate(canvas::manager& canvases) {
    canvases.set_drag_mode(ui::canvas::drag_mode::none);
}

void ui::tool::pan::activate(canvas::manager& canvases) {
    canvases.set_drag_mode(ui::canvas::drag_mode::pan);
}