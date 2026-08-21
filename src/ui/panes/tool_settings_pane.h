#pragma once

#include <QWidget>
#include <QtWidgets>
#include <memory>

/*------------------------------------------------------------------------------------------------*/

namespace ui {

    namespace tool {
        class manager;
        class base;
    }

    namespace pane {

        class tool_settings : public QDockWidget {
        private:
            void on_current_tool_changed(tool::base& new_tool);

            QScrollArea* scroll_area_;
            QWidget* contents_host_;
            QVBoxLayout* contents_layout_;
            QWidget* current_contents_;
        public:
            tool_settings(QMainWindow* wnd);
            void set_tool(QString tool_name, QWidget* contents);
            void init(tool::manager& tool_mgr);
        };

    }
}