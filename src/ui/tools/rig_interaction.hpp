#pragma once

#include <QWidget>
#include <QtWidgets>
#include <functional>
#include <optional>

#include "drag_state.hpp"
#include "select_tool_panel.hpp"
#include "../../core/sm_animation.hpp"

namespace mdl {
    class project;
}

namespace ui {
    namespace canvas {
        class manager;
        class scene;
    }

    namespace tool {

        class rig_interaction {
        public:
            using authored_action = sm::action_data;

            enum class purpose {
                edit_project,
                author_animation
            };

            struct animation_authoring {
                sm::object_id character_root_bone;
                sm::point animation_root_origin{};
                double animation_root_angle = 0.0;
                std::function<void(const authored_action&)> begin;
                std::function<void(const authored_action&)> update;
                std::function<void(const authored_action&)> complete;
                std::function<void()> cancel;
                std::function<void(QString)> reject;
            };

            explicit rig_interaction(purpose use);

            void init(canvas::manager& canvases, mdl::project& model);
            void activate(canvas::manager& canvases);
            void deactivate(canvas::manager& canvases);

            void keyPressEvent(canvas::scene& c, QKeyEvent* event);
            void keyReleaseEvent(canvas::scene& c, QKeyEvent* event);
            void mousePressEvent(canvas::scene& c, QGraphicsSceneMouseEvent* event);
            void mouseMoveEvent(canvas::scene& c, QGraphicsSceneMouseEvent* event);
            void mouseReleaseEvent(canvas::scene& c, QGraphicsSceneMouseEvent* event);

            QWidget* settings_widget();
            void set_animation_authoring(std::optional<animation_authoring> authoring);

        private:
            purpose purpose_;
            select_tool_panel* settings_panel_ = nullptr;
            std::optional<drag_state> drag_;
            std::optional<QPointF> click_pt_;
            mdl::project* project_ = nullptr;
            canvas::manager* canvases_ = nullptr;
            std::optional<animation_authoring> animation_authoring_;

            bool authoring_animation() const;
            bool is_dragging() const;
            void pin_selection();
            void do_dragging(canvas::scene& canv, QPointF pt);
            void handle_rotation(canvas::scene& c, QPointF pt, rotation_state& ri);
            void handle_translation(canvas::scene& c, QPointF pt, translation_state& ri);
            void handle_click(canvas::scene& c, QPointF pt, bool shift_down, bool ctrl_down, bool alt_down);
            void handle_drag_complete(canvas::scene& c, bool shift_down, bool alt_down);
            void handle_select_drag(canvas::scene& canv, QRectF rect, bool shift_down, bool ctrl_down);
            void do_rotation_complete(canvas::scene& c, const rotation_state& ri);
            void do_translation_complete(canvas::scene& c, const translation_state& ri);

            std::optional<rubber_band_type> kind_of_rubber_band(canvas::scene& canv, QPointF pt);
            std::optional<drag_state> create_drag_state(
                rubber_band_type typ, canvas::scene& canv, QPointF pt) const;

            static std::optional<rotation_state> create_rotation_state(
                canvas::scene& canv, QPointF clicked_pt, const sel_drag_settings& settings);

            std::optional<translation_state> create_translation_state(
                canvas::scene& canv, QPointF clicked_pt, const sel_drag_settings& settings) const;

            void cancel_animation_drag(canvas::scene& canv);
        };
    }
}
