#pragma once

#include "tool.h"

namespace ui {

    class move_tool : public tool {
    public:

        struct move_state {

            canvas::canvas* canvas_;
            ui::canvas::node_item* anchor_;
            ui::canvas::bone_item* bone_;
            QGraphicsSceneMouseEvent* event_;
            QKeyEvent* key_event_;

            move_state();
            void set(canvas::canvas& canvas, QGraphicsSceneMouseEvent* event);
            void set(canvas::canvas& canvas, QKeyEvent* event);
        };

        move_tool();
        void keyPressEvent(canvas::canvas& c, QKeyEvent* event) override;
        void mousePressEvent(canvas::canvas& c, QGraphicsSceneMouseEvent* event) override;
        void mouseMoveEvent(canvas::canvas& c, QGraphicsSceneMouseEvent* event) override;
        void mouseReleaseEvent(canvas::canvas& c, QGraphicsSceneMouseEvent* event) override;
        QWidget* settings_widget() override;

    private: 
        QWidget* settings_;
        QButtonGroup* btns_;
        move_state state_;
    };

}