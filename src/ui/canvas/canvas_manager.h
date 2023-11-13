#pragma once

#include <QWidget>
#include <QtWidgets>
#include "canvas.h"

class input_handler;

namespace ui {

    namespace canvas {

        class canvas_manager : public QTabWidget {

            Q_OBJECT

        private:
            QGraphicsView& active_view() const;
            canvas* active_canv_;
            QMetaObject::Connection current_tab_conn_;
            input_handler& inp_handler_;
            drag_mode drag_mode_;

            void connect_current_tab_signal();
            void disconnect_current_tab_signal();
            void add_tab(const std::string& name);
            void add_or_delete_tab(const std::string& name, bool should_add);
            void prepare_to_add_bone(sm::node& u, sm::node& v);
            void add_new_bone(sm::bone& bone);
            void add_new_skeleton(const std::string& canvas, sm::skel_ref skel);
            void set_contents(mdl::project& model);
            void set_contents_of_canvas(mdl::project& model, const std::string& canvas);
            void clear_canvas(const std::string& canv);

        public:
            canvas_manager(input_handler& inp_handler);
            void init(mdl::project& proj);
            void clear();
            void center_active_view();
            canvas& active_canvas() const;
            canvas* canvas_from_name(const std::string& canv_name);
            void set_drag_mode(drag_mode dm);
            void set_active_canvas(const canvas& c);
            std::vector<std::string> tab_names() const;
            std::string tab_name(const canvas& canv) const;

            auto canvases() {
                namespace r = std::ranges;
                namespace rv = std::ranges::views;
                return rv::iota(0, count()) |
                    rv::transform(
                        [this](int i)->ui::canvas::canvas* {
                            return static_cast<ui::canvas::canvas*>(
                                static_cast<QGraphicsView*>(widget(i))->scene()
                                );
                        }
                );
            }

        signals:
            void active_canvas_changed(ui::canvas::canvas& old_canv, ui::canvas::canvas& canv);
            void selection_changed(ui::canvas::canvas& canv);
            void rubber_band_change(ui::canvas::canvas& canv, QRect rbr, QPointF from, QPointF to);
            void canvas_refresh(sm::world& proj);
        };
    }
}