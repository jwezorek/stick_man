#pragma once

#include "tools.h"
#include <QWidget>
#include <QtWidgets>
#include <optional>


namespace ui {

    class selection_tool : public abstract_tool {
    private:
        std::optional<QRectF> rubber_band_;
        QMetaObject::Connection conn_;

        void handle_click(canvas& c, QPointF pt, bool shift_down, bool alt_down);
        void handle_drag(canvas& c, QRectF rect, bool shift_down, bool alt_down);

    public:
        selection_tool();
        void activate(canvas& c) override;
        void mousePressEvent(canvas& c, QGraphicsSceneMouseEvent* event) override;
        void mouseReleaseEvent(canvas& c, QGraphicsSceneMouseEvent* event) override;
        void deactivate(canvas& c) override;
    };

}