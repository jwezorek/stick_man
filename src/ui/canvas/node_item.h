#pragma once

#include "canvas_item.h"

namespace ui {

    class node_item :
        public has_stick_man_model<node_item, sm::node&>, public QGraphicsEllipseItem {
    private:
        QGraphicsEllipseItem* pin_;
        void sync_item_to_model() override;
        void sync_sel_frame_to_model() override;
        QGraphicsItem* create_selection_frame() const override;
        bool is_selection_frame_only() const override;
        QGraphicsItem* item_body() override;
        mdl::const_skel_piece to_skeleton_piece() const override;

        bool is_pinned_;
    public:
        using model_type = sm::node;

        node_item(sm::node& node, double scale);
        void set_pinned(bool pinned);
        bool is_pinned() const;
    };

}