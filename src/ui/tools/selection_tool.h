#pragma once

#include "tool.h"
#include <QWidget>
#include <QtWidgets>
#include <optional>
#include <unordered_set>
#include <variant>
#include <memory>
#include "select_tool_panel.h"

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

        using node_locs = std::vector<std::tuple<mdl::handle, sm::point>>;

        class select : public base {
        private:
            enum rubber_band_type {
                selection_rb,
                translation_rb,
                rotation_rb
            };

            struct rot_info {
                sm::node_ref axis;
                sm::node_ref rotating;
                sm::bone_ref bone;
                double initial_theta;
                std::unique_ptr<node_locs> old_locs;
            };

            struct trans_info {
                std::vector<sm::node_ref> selected;
                std::vector<sm::node_ref> pinned;
            };

            struct drag_state {
                QPointF pt;
                canvas::item::rubber_band* rubber_band;
                rubber_band_type type;
                std::variant<std::monostate, rot_info, trans_info> extra;
            };

            select_tool_panel* settings_panel_;
            std::optional<drag_state> drag_;
            std::optional<QPointF> click_pt_;
            mdl::project* project_;

            void do_rotation_complete(const rot_info& ri);
            void handle_rotation(canvas::scene& c, QPointF pt, const rot_info& ri);
            void handle_translation(canvas::scene& c, const trans_info& ri);
            void handle_click(canvas::scene& c, QPointF pt, bool shift_down, bool alt_down);
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
            static std::optional<rot_info> get_rotation_info(ui::canvas::scene& canv, QPointF clicked_pt,
                const ui::tool::sel_drag_settings& settings);

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