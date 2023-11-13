#pragma once

#include "props_box.h"
#include "../util.h"

namespace ui {
    namespace pane {
        namespace props {

            class skeletons : public props_box {
                labeled_field* name_;
            public:
                skeletons(const current_canvas_fn& fn, selection_properties* parent);
                void populate(mdl::project& proj) override;
                void set_selection(const ui::canvas::scene& canv) override;
            };

        }
    }
}