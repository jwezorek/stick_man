#pragma once

#include "tool.h"

namespace ui {

    class canvas_manager;

    class add_bone_tool : public tool {
    private:
        QPointF origin_;
        QGraphicsLineItem* rubber_band_;
        mdl::project* model_;

        void init_rubber_band(canvas& c);

    public:
        add_bone_tool();
        void init(canvas_manager& canvases, mdl::project& model) override;
        void mousePressEvent(canvas& c, QGraphicsSceneMouseEvent* event) override;
        void mouseMoveEvent(canvas& c, QGraphicsSceneMouseEvent* event) override;
        void mouseReleaseEvent(canvas& c, QGraphicsSceneMouseEvent* event) override;
    };
}