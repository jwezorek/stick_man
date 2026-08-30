#pragma once

#include <QWidget>
#include <QtWidgets>

/*------------------------------------------------------------------------------------------------*/

namespace ui {

    namespace pane {

        class animation : public QDockWidget {
        public:
            animation(QMainWindow* wnd);
        };

    }

}