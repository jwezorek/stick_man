#pragma once

#include "tool.h"

namespace ui {

    namespace canvas {
        class canvas_manager;
    }

    class pan_tool : public tool {
    public:
        pan_tool();
        void activate(canvas::canvas_manager& canvases) override;
        void deactivate(canvas::canvas_manager& canvases) override;
    };

}