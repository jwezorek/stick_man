#pragma once

#include "tool.h"

namespace ui {

    namespace canvas {
        class manager;
    }
    namespace tool {
        class pan_tool : public base {
        public:
            pan_tool();
            void activate(canvas::manager& canvases) override;
            void deactivate(canvas::manager& canvases) override;
        };
    }
}