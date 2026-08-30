#pragma once

#include "tool.hpp"

namespace ui {

    namespace canvas {
        class manager;
    }

    namespace tool {

        class pan : public base {
        public:
            pan();
            void activate(canvas::manager& canvases) override;
            void deactivate(canvas::manager& canvases) override;
        };

    }
}