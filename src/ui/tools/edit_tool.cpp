#include "edit_tool.hpp"

ui::tool::edit::edit() :
    base("edit", "arrow_icon.png", ui::tool::id::edit) {
}

void ui::tool::edit::init(canvas::manager& canvases, mdl::project& model) {
    interaction_.init(canvases, model);
}

void ui::tool::edit::activate(canvas::manager& canvases) {
    interaction_.activate(canvases);
}

void ui::tool::edit::deactivate(canvas::manager& canvases) {
    interaction_.deactivate(canvases);
}

void ui::tool::edit::keyPressEvent(canvas::scene& c, QKeyEvent* event) {
    interaction_.keyPressEvent(c, event);
}

void ui::tool::edit::keyReleaseEvent(canvas::scene& c, QKeyEvent* event) {
    interaction_.keyReleaseEvent(c, event);
}

void ui::tool::edit::mousePressEvent(canvas::scene& c, QGraphicsSceneMouseEvent* event) {
    interaction_.mousePressEvent(c, event);
}

void ui::tool::edit::mouseMoveEvent(canvas::scene& c, QGraphicsSceneMouseEvent* event) {
    interaction_.mouseMoveEvent(c, event);
}

void ui::tool::edit::mouseReleaseEvent(canvas::scene& c, QGraphicsSceneMouseEvent* event) {
    interaction_.mouseReleaseEvent(c, event);
}

QWidget* ui::tool::edit::settings_widget() {
    return interaction_.settings_widget();
}
