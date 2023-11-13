#pragma once

#include "tool.h"

namespace ui {

    namespace canvas {
        class manager;
    }

    class pan_tool : public tool {
    public:
        pan_tool();
        void activate(canvas::manager& canvases) override;
        void deactivate(canvas::manager& canvases) override;
    };

}