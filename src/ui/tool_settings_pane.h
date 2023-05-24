#pragma once

#include <QWidget>
#include <QtWidgets>
#include <memory>

/*------------------------------------------------------------------------------------------------*/

namespace ui {

    class FlowLayout;

    class tool_settings_pane : public QDockWidget {
    public:
        tool_settings_pane(QMainWindow* wnd);
        void set_tool(QString tool_name, QWidget* contents);
    };

}