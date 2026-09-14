#pragma once
#include "tool.hpp"
#include "../canvas/artwork_layer.hpp"

namespace ui::tool {
    class sprite_transform : public base {
        canvas::sprite_drag mode_ = canvas::sprite_drag::translate;
        QWidget* settings_ = nullptr;
    public:
        sprite_transform();
        QWidget* settings_widget() override;
        void activate(canvas::manager& canvases) override;
        void deactivate(canvas::manager& canvases) override;
        void mousePressEvent(canvas::scene& scene, QGraphicsSceneMouseEvent* event) override;
        void mouseMoveEvent(canvas::scene& scene, QGraphicsSceneMouseEvent* event) override;
        void mouseReleaseEvent(canvas::scene& scene, QGraphicsSceneMouseEvent* event) override;
        void keyPressEvent(canvas::scene& scene, QKeyEvent* event) override;
    };
}
