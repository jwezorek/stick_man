#pragma once

#include "tool.h"

namespace ui {

    namespace canvas {
        class manager;
    }

    namespace tool {
        class add_bone_tool : public base {
        private:
            QPointF origin_;
            QGraphicsLineItem* rubber_band_;
            mdl::project* model_;

            void init_rubber_band(canvas::scene& c);

        public:
            add_bone_tool();
            void init(canvas::manager& canvases, mdl::project& model) override;
            void mousePressEvent(canvas::scene& c, QGraphicsSceneMouseEvent* event) override;
            void mouseMoveEvent(canvas::scene& c, QGraphicsSceneMouseEvent* event) override;
            void mouseReleaseEvent(canvas::scene& c, QGraphicsSceneMouseEvent* event) override;
        };
    }
}