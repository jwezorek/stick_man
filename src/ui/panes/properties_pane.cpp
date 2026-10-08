#include "properties_pane.hpp"
#include "../stick_man.hpp"
#include "../canvas/canvas_manager.hpp"

ui::pane::properties_pane::properties_pane(ui::stick_man* window)
    : QDockWidget(tr("Properties"), window),
      properties_(new properties_widget(
          [window]() -> canvas::scene& { return window->canvases().active_canvas(); }, this)) {
    setWidget(properties_);
}

void ui::pane::properties_pane::init(canvas::manager& canvases, mdl::project& project) {
    canvases_ = &canvases;
    properties_->init(canvases, project);
}

void ui::pane::properties_pane::set_animation_mode(bool active) {
    properties_->set_read_only(active);
    if (!active && canvases_)
        properties_->set(canvases_->active_canvas());
}
