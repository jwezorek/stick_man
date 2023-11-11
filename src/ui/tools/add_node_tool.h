#pragma once

#include "tool.h"

namespace ui {

    class canvas_manager;

    class add_node_tool : public tool {
        mdl::project* model_;
    public:
        add_node_tool();
        void init(canvas_manager& canvases, mdl::project& model) override;
        void mouseReleaseEvent(canvas& c, QGraphicsSceneMouseEvent* event) override;
    };

}