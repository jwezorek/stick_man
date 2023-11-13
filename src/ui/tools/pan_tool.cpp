#include "pan_tool.h"
#include "../canvas/canvas_manager.h"

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