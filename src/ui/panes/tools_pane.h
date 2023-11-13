#pragma once

#include <QWidget>
#include <QtWidgets>
#include "../tools/tool.h"

/*------------------------------------------------------------------------------------------------*/

namespace ui {

    class tool_btn;
    
    namespace canvas {
        class manager;
    }

    namespace pane {
        class tools : public QDockWidget {
            Q_OBJECT

        private:

            constexpr static auto k_tool_dim = 64;
            tool::manager& tools_;
            void handle_tool_click(canvas::manager& canvases, tool_btn* btn);
            tool_btn* tool_from_id(tool::id id);

        public:

            tools(QMainWindow* wnd);
        };
    }
}