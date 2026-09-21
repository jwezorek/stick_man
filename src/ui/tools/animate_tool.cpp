#include "animate_tool.hpp"
#include <utility>

ui::tool::animate::animate() :
    base("animate", "move_icon.png", ui::tool::id::animate),
    interaction_(rig_interaction::purpose::author_animation) {
}

void ui::tool::animate::init(canvas::manager& canvases, mdl::project& model) {
    interaction_.init(canvases, model);
}

void ui::tool::animate::activate(canvas::manager& canvases) {
    interaction_.activate(canvases);
}

void ui::tool::animate::deactivate(canvas::manager& canvases) {
    interaction_.deactivate(canvases);
}

void ui::tool::animate::keyPressEvent(canvas::scene& c, QKeyEvent* event) {
    interaction_.keyPressEvent(c, event);
}

void ui::tool::animate::keyReleaseEvent(canvas::scene& c, QKeyEvent* event) {
    interaction_.keyReleaseEvent(c, event);
}

void ui::tool::animate::mousePressEvent(canvas::scene& c, QGraphicsSceneMouseEvent* event) {
    interaction_.mousePressEvent(c, event);
}

void ui::tool::animate::mouseMoveEvent(canvas::scene& c, QGraphicsSceneMouseEvent* event) {
    interaction_.mouseMoveEvent(c, event);
}

void ui::tool::animate::mouseReleaseEvent(canvas::scene& c, QGraphicsSceneMouseEvent* event) {
    interaction_.mouseReleaseEvent(c, event);
}

QWidget* ui::tool::animate::settings_widget() {
    return interaction_.settings_widget();
}

void ui::tool::animate::set_animation_authoring(std::optional<animation_authoring> authoring) {
    interaction_.set_animation_authoring(std::move(authoring));
}
