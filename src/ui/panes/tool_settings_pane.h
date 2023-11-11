#pragma once

#include <QWidget>
#include <QtWidgets>
#include <memory>

/*------------------------------------------------------------------------------------------------*/

namespace ui {

    class tool_manager;
    class tool;

    class tool_settings_pane : public QDockWidget {
    private:
        void on_current_tool_changed(tool& new_tool);
    public:
        tool_settings_pane(QMainWindow* wnd);
        void set_tool(QString tool_name, QWidget* contents);
        void init(tool_manager& tool_mgr);
    };

}