#include "selection_tool.hpp"

ui::tool::select::select() :
    base("selection", "arrow_icon.png", ui::tool::id::selection),
    interaction_(rig_interaction::purpose::edit_project) {
}

void ui::tool::select::init(canvas::manager& canvases, mdl::project& model) {
    interaction_.init(canvases, model);
}

void ui::tool::select::activate(canvas::manager& canvases) {
    interaction_.activate(canvases);
}

void ui::tool::select::deactivate(canvas::manager& canvases) {
    interaction_.deactivate(canvases);
}

void ui::tool::select::keyPressEvent(canvas::scene& c, QKeyEvent* event) {
    interaction_.keyPressEvent(c, event);
}

void ui::tool::select::keyReleaseEvent(canvas::scene& c, QKeyEvent* event) {
    interaction_.keyReleaseEvent(c, event);
}

void ui::tool::select::mousePressEvent(canvas::scene& c, QGraphicsSceneMouseEvent* event) {
    interaction_.mousePressEvent(c, event);
}

void ui::tool::select::mouseMoveEvent(canvas::scene& c, QGraphicsSceneMouseEvent* event) {
    interaction_.mouseMoveEvent(c, event);
}

void ui::tool::select::mouseReleaseEvent(canvas::scene& c, QGraphicsSceneMouseEvent* event) {
    interaction_.mouseReleaseEvent(c, event);
}

QWidget* ui::tool::select::settings_widget() {
    return interaction_.settings_widget();
}
