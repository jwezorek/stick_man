#pragma once

#include "properties.h"
#include "../util.h"

namespace props {
    class skeleton_properties : public ui::abstract_properties_widget {
        ui::labeled_field* name_;
    public:
        skeleton_properties(const ui::current_canvas_fn& fn, ui::selection_properties* parent);
        void populate(mdl::project& proj) override;
        void set_selection(const ui::canvas& canv) override;
    };
}