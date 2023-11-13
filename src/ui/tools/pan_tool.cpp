#include "pan_tool.h"
#include "../canvas/canvas_manager.h"

/*------------------------------------------------------------------------------------------------*/

ui::tool::pan_tool::pan_tool() :
    tool("pan", "pan_icon.png", ui::tool::tool_id::pan)
{}

void ui::tool::pan_tool::deactivate(canvas::manager& canvases) {
    canvases.set_drag_mode(ui::canvas::drag_mode::none);
}

void ui::tool::pan_tool::activate(canvas::manager& canvases) {
    canvases.set_drag_mode(ui::canvas::drag_mode::pan);
}