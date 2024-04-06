#pragma once

namespace ui {
    namespace tool {

        using node_locs = std::vector<std::tuple<mdl::handle, sm::point>>;

        class rotation_state {
        public:
            sm::node_ref axis;
            sm::node_ref rotating;
            sm::bone_ref bone;
            double initial_theta;
            std::unique_ptr<node_locs> old_locs;
            sel_drag_mode mode;
        };

        struct translation_state {
            std::vector<sm::node_ref> selected;
            std::vector<sm::node_ref> pinned;
        };

        enum rubber_band_type {
            selection_rb,
            translation_rb,
            rotation_rb
        };

        struct drag_state {
            QPointF pt;
            canvas::item::rubber_band* rubber_band;
            rubber_band_type type;
            std::variant<std::monostate, rotation_state, translation_state> extra;
        };
    }
}