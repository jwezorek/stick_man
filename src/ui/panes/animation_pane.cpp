#include "animation_pane.hpp"
#include <QIcon>

/*------------------------------------------------------------------------------------------------*/

namespace {

}

ui::pane::animation::animation(QMainWindow* wnd) :
        QDockWidget(tr("Animation"), wnd) {
    setWindowIcon(QIcon(":/images/move_icon.png"));

    setWidget(new QWidget());
}
