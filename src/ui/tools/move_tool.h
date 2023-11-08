#pragma once

#include "tools.h"

namespace ui {

    class move_tool : public abstract_tool {
    public:

        struct move_state {

            canvas* canvas_;
            ui::node_item* anchor_;
            ui::bone_item* bone_;
            QGraphicsSceneMouseEvent* event_;
            QKeyEvent* key_event_;

            move_state();
            void set(canvas& canvas, QGraphicsSceneMouseEvent* event);
            void set(canvas& canvas, QKeyEvent* event);
        };

        move_tool();
        void keyPressEvent(canvas& c, QKeyEvent* event) override;
        void mousePressEvent(canvas& c, QGraphicsSceneMouseEvent* event) override;
        void mouseMoveEvent(canvas& c, QGraphicsSceneMouseEvent* event) override;
        void mouseReleaseEvent(canvas& c, QGraphicsSceneMouseEvent* event) override;
        QWidget* settings_widget() override;

    private: 
        QWidget* settings_;
        QButtonGroup* btns_;
        move_state state_;
    };

}