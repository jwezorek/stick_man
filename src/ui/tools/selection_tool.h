#pragma once

#include "tools.h"
#include <QWidget>
#include <QtWidgets>
#include <optional>
#include <unordered_set>

/*------------------------------------------------------------------------------------------------*/

namespace ui {

    using selection_set = std::unordered_set<ui::canvas_item*>;

	class canvas_manager;
    class project;

    class selection_tool : public abstract_tool {
    private:

        std::optional<QRectF> rubber_band_;

        void handle_click(canvas& c, QPointF pt, bool shift_down, bool alt_down);
        void handle_drag(canvas& c, QRectF rect, bool shift_down, bool alt_down);
    public:

        selection_tool();
        void activate(canvas_manager& c) override;

        void keyReleaseEvent(canvas& c, QKeyEvent* event) override;
        void mousePressEvent(canvas& c, QGraphicsSceneMouseEvent* event) override;
        void mouseMoveEvent(canvas& c, QGraphicsSceneMouseEvent* event) override;
        void mouseReleaseEvent(canvas& c, QGraphicsSceneMouseEvent* event) override;
        void deactivate(canvas_manager& c) override;
        QWidget* settings_widget() override;
		void init(canvas_manager& canvases, mdl::project& model) override;
    };

}