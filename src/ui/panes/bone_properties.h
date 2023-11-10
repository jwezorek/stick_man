#pragma once

#include "properties.h"

namespace props {

    class rotation_tab;
    class rot_constraint_box;

    class bone_properties : public single_or_multi_props_widget {
        ui::labeled_numeric_val* length_;
        ui::labeled_field* name_;
        ui::labeled_hyperlink* u_;
        ui::labeled_hyperlink* v_;
        QWidget* nodes_;
        rotation_tab* rotation_;
        rot_constraint_box* constraint_box_;
        QPushButton* constraint_btn_;

        void add_or_delete_constraint(mdl::project& proj, ui::canvas& canv);

    public:
        bone_properties(const current_canvas_fn& fn, ui::selection_properties* parent);

        void populate(mdl::project& proj) override;
        bool is_multi(const ui::canvas& canv) override;
        void set_selection_common(const ui::canvas& canv) override;
        void set_selection_multi(const ui::canvas& canv) override;
        void set_selection_single(const ui::canvas& canv) override;
        void lose_selection() override;
    };

}