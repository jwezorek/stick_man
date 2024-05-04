#pragma once

#include "tool.h"

namespace ui {
    namespace tool {

        class animate : public base {
        public:
            animate();
            void keyPressEvent(canvas::scene& c, QKeyEvent* event) override;
            void mousePressEvent(canvas::scene& c, QGraphicsSceneMouseEvent* event) override;
            void mouseMoveEvent(canvas::scene& c, QGraphicsSceneMouseEvent* event) override;
            void mouseReleaseEvent(canvas::scene& c, QGraphicsSceneMouseEvent* event) override;
            QWidget* settings_widget() override;

        private:
        };
    }
}