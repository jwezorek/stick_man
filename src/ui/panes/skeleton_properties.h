#pragma once

#include "props_box.h"
#include "../util.h"

namespace props {
    class skeleton_properties : public props_box {
        ui::labeled_field* name_;
    public:
        skeleton_properties(const current_canvas_fn& fn, ui::selection_properties* parent);
        void populate(mdl::project& proj) override;
        void set_selection(const ui::canvas& canv) override;
    };
}