#pragma once

#include "canvas_item.h"

namespace ui {

    namespace canvas {
        namespace item {
            class skeleton_item :
                public has_treeview_item,
                public has_stick_man_model<skeleton_item, sm::skeleton&>,
                public QGraphicsRectItem {
            private:
                void sync_item_to_model() override;
                void sync_sel_frame_to_model() override;
                QGraphicsItem* create_selection_frame() const override;
                bool is_selection_frame_only() const override;
                QGraphicsItem* item_body() override;
                mdl::const_skel_piece to_skeleton_piece() const override;

            public:
                using model_type = sm::skeleton;
                skeleton_item(sm::skeleton& skel, double scale);
            };
        }
    }
}