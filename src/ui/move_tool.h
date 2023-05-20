#pragma once

#include "tools.h"

namespace ui {

    namespace detail {
        struct rotate_state {

        };
    }

    class move_tool : public abstract_tool {
    private:
        std::unique_ptr<QButtonGroup> btns_;
        detail::rotate_state rotate_;
    public:
        move_tool();
        void mousePressEvent(canvas& c, QGraphicsSceneMouseEvent* event) override;
        void mouseMoveEvent(canvas& c, QGraphicsSceneMouseEvent* event) override;
        void mouseReleaseEvent(canvas& c, QGraphicsSceneMouseEvent* event) override;
        void populate_settings_aux(tool_settings_pane* pane) override;
    };

}