#pragma once

#include "canvas_item.hpp"

namespace ui {

    namespace canvas {
        namespace item {
            class node :
                public has_stick_man_model<node, sm::node&>, public QGraphicsEllipseItem {
            private:
                QGraphicsEllipseItem* pin_;
                bool wireframe_ = false;
                void apply_display_style(double scale);
                void sync_item_to_model() override;
                void sync_sel_frame_to_model() override;
                QGraphicsItem* create_selection_frame() const override;
                bool is_selection_frame_only() const override;
                QGraphicsItem* item_body() override;
                mdl::const_skel_piece to_skeleton_piece() const override;

                bool pin_visible_;
            public:
                using model_type = sm::node;

                node(sm::node& node, double scale);
                void set_wireframe(bool wireframe);
                void set_pin_visible(bool visible);
                bool pin_visible() const;
            };
        }
    }
}