#pragma once
#include <QToolBar>
#include "../tools/tool.h"

/*------------------------------------------------------------------------------------------------*/

class QMainWindow;

namespace ui {

    class tool_btn;

    namespace canvas {
        class manager;
    }

    namespace pane {
        class tools : public QToolBar {
            Q_OBJECT

        private:
            tool::manager& tools_;
            void handle_tool_click(canvas::manager& canvases, tool_btn* btn);
            tool_btn* tool_from_id(tool::id id);

        public:

            tools(QMainWindow* wnd);
        };
    }
}
