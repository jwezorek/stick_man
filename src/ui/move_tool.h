#pragma once

#include "tools.h"

namespace ui {

    class move_tool : public abstract_tool {
    public:

        struct move_state {

            canvas* canvas_;
            QPointF anchor_;
            QGraphicsItem* rubber_band_;
            QGraphicsSceneMouseEvent* event_;

            move_state();
            void set(canvas& canvas, QGraphicsSceneMouseEvent* event);
        };

        move_tool();
        void mousePressEvent(canvas& c, QGraphicsSceneMouseEvent* event) override;
        void mouseMoveEvent(canvas& c, QGraphicsSceneMouseEvent* event) override;
        void mouseReleaseEvent(canvas& c, QGraphicsSceneMouseEvent* event) override;
        void populate_settings_aux(tool_settings_pane* pane) override;

    private: 

        std::unique_ptr<QButtonGroup> btns_;
        move_state state_;
    };

}