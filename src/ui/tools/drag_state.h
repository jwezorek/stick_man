#pragma once

#include <memory>
#include "../../model/handle.h"
#include "../../core/sm_types.h"

namespace ui {

    namespace canvas {
        class manager;
        namespace item {
            class rubber_band;
        }
    }

    namespace tool {

        enum class sel_drag_mode {
            rag_doll,
            rubber_band,
            rigid,
            unique
        };

        using node_locs = std::vector<std::tuple<mdl::handle, sm::point>>;

        class rotation_state {
        private:

            sm::node_ref axis_;
            sm::node_ref rotating_;
            sm::bone_ref bone_;
            double initial_theta_;
            std::unique_ptr<node_locs> old_locs_;
            sel_drag_mode mode_;
            double radius_;

        public:
            rotation_state(
                sm::node_ref axis,
                sm::node_ref rotating,
                sm::bone_ref bone,
                double initial_theta,
                sel_drag_mode mode
            );

            const sm::node& axis() const;
            const sm::node& rotating() const;
            const sm::bone& bone() const;

            sm::node& axis();
            sm::node& rotating();
            sm::bone& bone();

            double initial_theta() const;
            const node_locs& old_node_locs() const;
            node_locs current_node_locs() const;
            sel_drag_mode mode() const;
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
            sm::point pt;
            canvas::item::rubber_band* rubber_band;
            rubber_band_type type;
            std::variant<std::monostate, rotation_state, translation_state> extra;
        };
    }
}