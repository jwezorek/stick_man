#pragma once

#include "tool.h"

namespace ui {

    namespace canvas {
        class manager;
    }

    namespace tool {

        class add_node : public base {
            mdl::project* model_;
        public:
            add_node();
            void init(canvas::manager& canvases, mdl::project& model) override;
            void mouseReleaseEvent(canvas::scene& c, QGraphicsSceneMouseEvent* event) override;
        };

    }
}