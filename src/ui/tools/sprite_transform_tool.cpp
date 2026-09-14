#include "sprite_transform_tool.hpp"
#include "../canvas/canvas_manager.hpp"

ui::tool::sprite_transform::sprite_transform() : base("Sprite Transform", "move_icon.png", id::sprite_transform) {}
QWidget* ui::tool::sprite_transform::settings_widget() {
    if (!settings_) {
        settings_ = new QWidget;
        auto* layout = new QVBoxLayout(settings_);
        auto* modes = new QComboBox(settings_); modes->setObjectName("sprite_transform_mode");
        modes->addItems({"Move", "Rotate", "Scale X", "Scale Y", "Scale X and Y"}); layout->addWidget(modes);
        auto* help = new QLabel("Drag a sprite to transform it. Rotation and scaling use the slot's origin. Escape cancels. Exact values are in Appearances.", settings_);
        help->setWordWrap(true); layout->addWidget(help); layout->addStretch();
        QObject::connect(modes, &QComboBox::currentIndexChanged, settings_, [this](int index) { mode_ = canvas::sprite_drag(index); });
    }
    return settings_;
}
void ui::tool::sprite_transform::activate(canvas::manager& canvases) { canvases.set_drag_mode(canvas::drag_mode::none); }
void ui::tool::sprite_transform::deactivate(canvas::manager& canvases) { canvases.active_canvas().artwork().cancel_transform(); }
void ui::tool::sprite_transform::mousePressEvent(canvas::scene& scene, QGraphicsSceneMouseEvent* event) {
    if (event->button() == Qt::LeftButton) { scene.artwork().begin_transform(event->scenePos(), mode_); event->accept(); }
}
void ui::tool::sprite_transform::mouseMoveEvent(canvas::scene& scene, QGraphicsSceneMouseEvent* event) {
    scene.artwork().update_transform(event->scenePos()); event->accept();
}
void ui::tool::sprite_transform::mouseReleaseEvent(canvas::scene& scene, QGraphicsSceneMouseEvent* event) {
    if (event->button() == Qt::LeftButton) { scene.artwork().end_transform(event->scenePos()); event->accept(); }
}
void ui::tool::sprite_transform::keyPressEvent(canvas::scene& scene, QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape) { scene.artwork().cancel_transform(); event->accept(); }
}
