#pragma once

#include "tool.h"
#include <QWidget>
#include <QtWidgets>
#include <optional>
#include <unordered_set>

/*------------------------------------------------------------------------------------------------*/

namespace ui {

    using selection_set = std::unordered_set<ui::canvas::item::base*>;

    namespace canvas {
        class manager;
    }

    namespace tool {

        class select : public base {
        private:

            std::optional<QRectF> rubber_band_;

            void handle_click(canvas::scene& c, QPointF pt, bool shift_down, bool alt_down);
            void handle_drag(canvas::scene& c, QRectF rect, bool shift_down, bool alt_down);
        public:

            select();
            void activate(canvas::manager& c) override;

            void keyReleaseEvent(canvas::scene& c, QKeyEvent* event) override;
            void mousePressEvent(canvas::scene& c, QGraphicsSceneMouseEvent* event) override;
            void mouseMoveEvent(canvas::scene& c, QGraphicsSceneMouseEvent* event) override;
            void mouseReleaseEvent(canvas::scene& c, QGraphicsSceneMouseEvent* event) override;
            void deactivate(canvas::manager& c) override;
            QWidget* settings_widget() override;
            void init(canvas::manager& canvases, mdl::project& model) override;
        };
    }

}