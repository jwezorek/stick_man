#pragma once

#include "canvas_item.h"

namespace ui {

    namespace canvas {

        namespace item {
            class node;
            class rot_constraint_adornment;

            class bone_item :
                public has_treeview_item,
                public has_stick_man_model<bone_item, sm::bone&>,
                public QGraphicsPolygonItem {
            private:
                rot_constraint_adornment* rot_constraint_;
                QStandardItem* treeview_item_;

                void sync_item_to_model() override;
                void sync_sel_frame_to_model() override;
                QGraphicsItem* create_selection_frame() const override;
                bool is_selection_frame_only() const override;
                QGraphicsItem* item_body() override;
                void sync_rotation_constraint_to_model();
                mdl::const_skel_piece to_skeleton_piece() const override;

            public:
                using model_type = sm::bone;

                bone_item(sm::bone& bone, double scale);
                item::node& parent_node_item() const;
                item::node& child_node_item() const;
            };

            Q_DECLARE_METATYPE(bone_item*);

            class rot_constraint_adornment :
                public QGraphicsEllipseItem {

            public:
                rot_constraint_adornment();
                void set(
                    const sm::bone& node,
                    const sm::rot_constraint& constraint,
                    double scale
                );
            };
        }
    }
}