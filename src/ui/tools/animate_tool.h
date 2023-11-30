#pragma once

#include "tool.h"

namespace ui {
    namespace tool {

        class animate : public base {
        public:

            struct move_state {

                canvas::scene* canvas_;
                ui::canvas::item::node* anchor_;
                ui::canvas::item::bone* bone_;
                QGraphicsSceneMouseEvent* event_;
                QKeyEvent* key_event_;

                move_state();
                void set(canvas::scene& canvas, QGraphicsSceneMouseEvent* event);
                void set(canvas::scene& canvas, QKeyEvent* event);
            };

            animate();
            void keyPressEvent(canvas::scene& c, QKeyEvent* event) override;
            void mousePressEvent(canvas::scene& c, QGraphicsSceneMouseEvent* event) override;
            void mouseMoveEvent(canvas::scene& c, QGraphicsSceneMouseEvent* event) override;
            void mouseReleaseEvent(canvas::scene& c, QGraphicsSceneMouseEvent* event) override;
            QWidget* settings_widget() override;

        private:
            QWidget* settings_;
            QButtonGroup* btns_;
            move_state state_;
        };
    }
}