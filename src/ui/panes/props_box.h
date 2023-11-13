#pragma once

#include <QtWidgets>
#include <unordered_map>
#include <functional>
#include <ranges>
#include "../util.h"
#include "../../model/project.h"

namespace ui {
    namespace canvas {
        class scene;
    }

    namespace pane {
        class selection_properties;

        namespace props {

            using current_canvas_fn = std::function<ui::canvas::scene& ()>;
            class nodes;
            class bones;

            class props_box : public QWidget {
            protected:
                QVBoxLayout* layout_;
                QLabel* title_;
                current_canvas_fn get_current_canv_;
                ui::pane::selection_properties* parent_;
                mdl::project* proj_;

                void do_property_name_change(const std::string& new_name);
                void handle_rename(mdl::skel_piece p, ui::string_edit* name_edit,
                    const std::string& new_name);

            public:
                props_box(const current_canvas_fn& fn,
                    ui::pane::selection_properties* parent, QString title);
                void set_title(QString title);
                void init(mdl::project& proj);
                virtual void populate(mdl::project& proj);
                virtual void set_selection(const ui::canvas::scene& canv) = 0;
                virtual void lose_selection();
            };

            class single_or_multi_props_widget : public props_box {
            public:
                single_or_multi_props_widget(const current_canvas_fn& fn, selection_properties* parent,
                    QString title);
                void set_selection(const ui::canvas::scene& canv) override;
                virtual void set_selection_common(const ui::canvas::scene& canv) = 0;
                virtual void set_selection_single(const ui::canvas::scene& canv) = 0;
                virtual void set_selection_multi(const ui::canvas::scene& canv) = 0;
                virtual bool is_multi(const ui::canvas::scene& canv) = 0;
            };

            class no_properties : public props_box {
            public:
                no_properties(const current_canvas_fn& fn, selection_properties* parent);
                void set_selection(const ui::canvas::scene& canv) override;
            };

            class mixed_properties : public props_box {
            private:
                nodes* nodes_;
                bones* bones_;
            public:
                mixed_properties(const current_canvas_fn& fn, selection_properties* parent);
                void populate(mdl::project& proj) override;
                void set_selection(const ui::canvas::scene& canv) override;
                void lose_selection() override;
            };
        }
    }
}

