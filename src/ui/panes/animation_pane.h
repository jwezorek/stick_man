#pragma once

#include <QWidget>
#include <QtWidgets>

/*------------------------------------------------------------------------------------------------*/

namespace ui {

    class animation_pane : public QDockWidget {
    public:
        animation_pane(QMainWindow* wnd);
    };

}