#pragma once

#include <QWidget>
#include <QtWidgets>

/*------------------------------------------------------------------------------------------------*/

namespace ui {

    class properties_pane : public QDockWidget {
    public:
        properties_pane(QMainWindow* wnd);
    };

}