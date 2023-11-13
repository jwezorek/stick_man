#pragma once

#include "tool.h"
#include <QWidget>
#include <QtWidgets>
#include <optional>
#include <unordered_set>

/*------------------------------------------------------------------------------------------------*/

namespace ui {

    using selection_set = std::unordered_set<ui::canvas::canvas_item*>;

    namespace canvas {
        class canvas_manager;
    }

    class project;

    class selection_tool : public tool {
    private:

        std::optional<QRectF> rubber_band_;

        void handle_click(canvas::canvas& c, QPointF pt, bool shift_down, bool alt_down);
        void handle_drag(canvas::canvas& c, QRectF rect, bool shift_down, bool alt_down);
    public:

        selection_tool();
        void activate(canvas::canvas_manager& c) override;

        void keyReleaseEvent(canvas::canvas& c, QKeyEvent* event) override;
        void mousePressEvent(canvas::canvas& c, QGraphicsSceneMouseEvent* event) override;
        void mouseMoveEvent(canvas::canvas& c, QGraphicsSceneMouseEvent* event) override;
        void mouseReleaseEvent(canvas::canvas& c, QGraphicsSceneMouseEvent* event) override;
        void deactivate(canvas::canvas_manager& c) override;
        QWidget* settings_widget() override;
		void init(canvas::canvas_manager& canvases, mdl::project& model) override;
    };

}