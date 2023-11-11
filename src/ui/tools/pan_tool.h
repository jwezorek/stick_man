#pragma once

#include "tool.h"

namespace ui {

    class canvas_manager;

    class pan_tool : public tool {
    public:
        pan_tool();
        void activate(canvas_manager& canvases) override;
        void deactivate(canvas_manager& canvases) override;
    };

}