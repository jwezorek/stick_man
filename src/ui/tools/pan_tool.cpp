#include "pan_tool.h"
#include "../canvas/canvas_manager.h"

/*------------------------------------------------------------------------------------------------*/

ui::pan_tool::pan_tool() :
    tool("pan", "pan_icon.png", ui::tool_id::pan)
{}

void ui::pan_tool::deactivate(canvas::canvas_manager& canvases) {
    canvases.set_drag_mode(ui::canvas::drag_mode::none);
}

void ui::pan_tool::activate(canvas::canvas_manager& canvases) {
    canvases.set_drag_mode(ui::canvas::drag_mode::pan);
}