#pragma once

#include <QWidget>
#include <QtWidgets>
#include <memory>

/*------------------------------------------------------------------------------------------------*/

namespace ui {

    class tool_manager;
    class tool;

    namespace pane {

        class tool_settings : public QDockWidget {
        private:
            void on_current_tool_changed(tool& new_tool);
        public:
            tool_settings(QMainWindow* wnd);
            void set_tool(QString tool_name, QWidget* contents);
            void init(tool_manager& tool_mgr);
        };

    }
}