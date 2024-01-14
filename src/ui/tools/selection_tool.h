#pragma once

#include "tool.h"
#include <QWidget>
#include <QtWidgets>
#include <optional>
#include <unordered_set>
#include <variant>

/*------------------------------------------------------------------------------------------------*/

namespace ui {

    using selection_set = std::unordered_set<ui::canvas::item::base*>;

    namespace canvas {
        class manager;
        namespace item {
            class rubber_band;
        }
    }

    namespace tool {

        class select_tool_panel;

        class select : public base {
        private:
            enum rubber_band_type {
                selection_rb,
                translation_rb,
                rotation_rb
            };

            struct drag_info {
                QPointF pt;
                canvas::item::rubber_band* rubber_band_;
                rubber_band_type type;
            };

            select_tool_panel* settings_panel_;
            std::optional<drag_info> drag_;
            std::optional<QPointF> click_pt_;

            void handle_click(canvas::scene& c, QPointF pt, bool shift_down, bool alt_down);
            void handle_drag_complete(canvas::scene& c, bool shift_down, bool alt_down);
            void handle_select_drag(
                canvas::scene& canv, QRectF rect, bool shift_down, bool ctrl_down
            );
            std::optional<rubber_band_type> kind_of_rubber_band( canvas::scene& canv, QPointF pt );
            static ui::canvas::item::rubber_band* create_rubber_band(
                rubber_band_type typ, ui::canvas::scene& canv, QPointF pt
            );
            bool is_dragging() const;
            void do_dragging(canvas::scene& canv, QPointF pt);

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