#pragma once

#include "tool.h"

namespace ui {

    namespace canvas {
        class manager;
    }
    namespace tool {

        class add_node_tool : public tool {
            mdl::project* model_;
        public:
            add_node_tool();
            void init(canvas::manager& canvases, mdl::project& model) override;
            void mouseReleaseEvent(canvas::scene& c, QGraphicsSceneMouseEvent* event) override;
        };

    }
}