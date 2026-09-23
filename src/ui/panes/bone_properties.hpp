#pragma once

#include "properties.hpp"

namespace ui {
    namespace pane {
        namespace props {

            class rotation_tab;

            class bones : public single_or_multi_props_widget {
                ui::labeled_numeric_val* length_;
                ui::labeled_field* name_;
                ui::labeled_hyperlink* u_;
                ui::labeled_hyperlink* v_;
                QWidget* nodes_;
                rotation_tab* rotation_;
                QPushButton* character_root_btn_;

            public:
                bones(const current_canvas_fn& fn, selection_properties* parent);

                void populate(mdl::project& proj) override;
                bool is_multi(const ui::canvas::scene& canv) override;
                void set_selection_common(const ui::canvas::scene& canv) override;
                void set_selection_multi(const ui::canvas::scene& canv) override;
                void set_selection_single(const ui::canvas::scene& canv) override;
                void lose_selection() override;
            };
        }
    }
}
