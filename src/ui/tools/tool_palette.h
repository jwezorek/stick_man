#pragma once

#include <QWidget>
#include <QtWidgets>
#include "tool.h"

/*------------------------------------------------------------------------------------------------*/

namespace ui {

    class tool_btn;
    
    namespace canvas {
        class canvas_manager;
    }

    class tool_palette : public QDockWidget {

        Q_OBJECT

    private:

        constexpr static auto k_tool_dim = 64;
        tool_manager& tools_;
        void handle_tool_click(canvas::canvas_manager& canvases, tool_btn* btn);
        tool_btn* tool_from_id(tool_id id);

    public:

        tool_palette(QMainWindow* wnd);
    };

}