#pragma once

#include "properties.h"
#include "../util.h"

namespace props {

    class node_position_tab;

    class node_properties : public single_or_multi_props_widget {
        ui::labeled_field* name_;
        node_position_tab* positions_;

        static double world_coordinate_to_rel(int index, double val);

    public:
        node_properties(const current_canvas_fn& fn, ui::selection_properties* parent);

        void populate(mdl::project& proj) override;
        void set_selection_common(const ui::canvas& canv) override;
        void set_selection_single(const ui::canvas& canv) override;
        void set_selection_multi(const ui::canvas& canv) override;
        bool is_multi(const ui::canvas& canv) override;
        void lose_selection() override;
    };

}