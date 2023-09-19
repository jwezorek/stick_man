#pragma once

#include "tools.h"
#include <QWidget>
#include <QtWidgets>
#include <optional>
#include <unordered_set>

/*------------------------------------------------------------------------------------------------*/

namespace ui {

    using selection_set = std::unordered_set<ui::abstract_canvas_item*>;

	class stick_man;

    class selection_tool : public abstract_tool {
    private:

        std::optional<QRectF> rubber_band_;
        QMetaObject::Connection conn_;
		stick_man& main_wnd_;

        void handle_click(canvas& c, QPointF pt, bool shift_down, bool alt_down);
        void handle_drag(canvas& c, QRectF rect, bool shift_down, bool alt_down);
        void handle_sel_changed(const canvas& canv);

    public:

        selection_tool(tool_manager* mgr, ui::stick_man* main_wnd );
        void activate(canvas& c) override;

        void keyReleaseEvent(canvas& c, QKeyEvent* event) override;
        void mousePressEvent(canvas& c, QGraphicsSceneMouseEvent* event) override;
        void mouseMoveEvent(canvas& c, QGraphicsSceneMouseEvent* event) override;
        void mouseReleaseEvent(canvas& c, QGraphicsSceneMouseEvent* event) override;
        void deactivate(canvas& c) override;
        QWidget* settings_widget() override;
		void init() override;
    };

}