#pragma once

#include <QWidget>
#include <QtWidgets>
#include <memory>

/*------------------------------------------------------------------------------------------------*/

namespace ui {

    namespace tool {
        class tool_manager;
        class base;
    }

    namespace pane {

        class tool_settings : public QDockWidget {
        private:
            void on_current_tool_changed(tool::base& new_tool);
        public:
            tool_settings(QMainWindow* wnd);
            void set_tool(QString tool_name, QWidget* contents);
            void init(tool::tool_manager& tool_mgr);
        };

    }
}