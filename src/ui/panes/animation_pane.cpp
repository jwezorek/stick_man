#include "animation_pane.h"
#include "../native_title_bar.h"
#include <QIcon>

/*------------------------------------------------------------------------------------------------*/

namespace {

}

ui::pane::animation::animation(QMainWindow* wnd) :
        QDockWidget(tr("Animation"), wnd) {
    setWindowIcon(QIcon(":/images/move_icon.png"));
    native_title_bar::install(this);

    setWidget(new QWidget());
}
