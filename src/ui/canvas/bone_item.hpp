#pragma once

#include "canvas_item.hpp"

namespace ui {

    namespace canvas {

        namespace item {
            class node;
            class bone :
                public has_treeview_item,
                public has_stick_man_model<bone, sm::bone&>,
                public QGraphicsPolygonItem {
            private:
                QStandardItem* treeview_item_;
                bool wireframe_ = false;

                void apply_display_style(double scale);
                void sync_item_to_model() override;
                void sync_sel_frame_to_model() override;
                QGraphicsItem* create_selection_frame() const override;
                bool is_selection_frame_only() const override;
                QGraphicsItem* item_body() override;
                mdl::const_skel_piece to_skeleton_piece() const override;
                void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

            public:
                using model_type = sm::bone;

                bone(sm::bone& bone, double scale);
                void set_wireframe(bool wireframe);
                item::node& parent_node_item() const;
                item::node& child_node_item() const;
            };

            Q_DECLARE_METATYPE(bone*);

        }
    }
}