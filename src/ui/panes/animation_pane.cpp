#include "animation_pane.h"
#include "../util.h"

/*------------------------------------------------------------------------------------------------*/

namespace {

}

ui::pane::animation::animation(QMainWindow* wnd) :
        QDockWidget(tr(""), wnd) {
    setTitleBarWidget( custom_title_bar("animation") );

    setWidget(new QWidget());
}