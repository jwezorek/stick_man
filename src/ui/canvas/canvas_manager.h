#pragma once

#include <QWidget>
#include <QtWidgets>
#include "scene.h"

namespace ui {

    namespace tool {
        class input_handler;
    }

    namespace canvas {

        class manager : public QTabWidget {

            Q_OBJECT

        private:
            QGraphicsView& active_view() const;
            scene* active_canv_;
            QMetaObject::Connection current_tab_conn_;
            tool::input_handler& inp_handler_;
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
            manager(tool::input_handler& inp_handler);
            void init(mdl::project& proj);
            void clear();
            void center_active_view();
            scene& active_canvas() const;
            scene* canvas_from_name(const std::string& canv_name);
            void set_drag_mode(drag_mode dm);
            void set_active_canvas(const scene& c);
            std::vector<std::string> tab_names() const;
            std::string tab_name(const scene& canv) const;

            auto canvases() {
                namespace r = std::ranges;
                namespace rv = std::ranges::views;
                return rv::iota(0, count()) |
                    rv::transform(
                        [this](int i)->ui::canvas::scene* {
                            return static_cast<ui::canvas::scene*>(
                                static_cast<QGraphicsView*>(widget(i))->scene()
                                );
                        }
                );
            }

        signals:
            void active_canvas_changed(ui::canvas::scene& old_canv, ui::canvas::scene& canv);
            void selection_changed(ui::canvas::scene& canv);
            void canvas_refresh(sm::world& proj);
        };
    }
}