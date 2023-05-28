#pragma once

#include <QWidget>
#include <QtWidgets>

/*------------------------------------------------------------------------------------------------*/

namespace ui {

    class skeleton_pane : public QDockWidget {
    public:
        skeleton_pane(QMainWindow* wnd);
    };

}