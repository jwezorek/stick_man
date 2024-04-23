#pragma once

#include "tool.h"
#include <QWidget>
#include <QtWidgets>
#include <optional>
#include <unordered_set>
#include <variant>
#include <memory>
#include "select_tool_panel.h"
#include "drag_state.h"

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

            select_tool_panel* settings_panel_;
            std::optional<drag_state> drag_;
            std::optional<QPointF> click_pt_;
            mdl::project* project_;
            canvas::manager* canvases_;

            void do_rotation_complete(const rotation_state& ri);
            void handle_rotation(canvas::scene& c, QPointF pt, rotation_state& ri);
            void handle_translation(canvas::scene& c, translation_state& ri);
            void handle_click(
                canvas::scene& c, QPointF pt, bool shift_down, bool ctrl_down, bool alt_down
            );
            void handle_drag_complete(canvas::scene& c, bool shift_down, bool alt_down);
            void handle_select_drag(
                canvas::scene& canv, QRectF rect, bool shift_down, bool ctrl_down
            );
            std::optional<rubber_band_type> kind_of_rubber_band( canvas::scene& canv, QPointF pt );
            std::optional<drag_state> create_drag_state(
                rubber_band_type typ, ui::canvas::scene& canv, QPointF pt
            ) const;
            bool is_dragging() const;
            void do_dragging(canvas::scene& canv, QPointF pt);
            static std::optional<rotation_state> get_rotation_state(
                ui::canvas::scene& canv, QPointF clicked_pt,
                const ui::tool::sel_drag_settings& settings);
            void pin_selection();

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