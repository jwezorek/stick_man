#pragma once

#include <QWidget>
#include <QtWidgets>

/*------------------------------------------------------------------------------------------------*/

namespace ui {

    class tool_settings_pane : public QDockWidget {
    public:
        tool_settings_pane(QMainWindow* wnd);
    };

}