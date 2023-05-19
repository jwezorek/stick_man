#pragma once

#include <QWidget>
#include <QtWidgets>

/*------------------------------------------------------------------------------------------------*/

namespace ui {

    class FlowLayout;

    class tool_settings_pane : public QDockWidget {
        QLayout* contents_;
    public:
        tool_settings_pane(QMainWindow* wnd);
        QLayout* contents();
    };

}