#include "animation_pane.h"
#include "util.h"

/*------------------------------------------------------------------------------------------------*/

namespace {

}

ui::animation_pane::animation_pane(QMainWindow* wnd) :
        QDockWidget(tr(""), wnd) {
    setTitleBarWidget( custom_title_bar("animation") );

    auto placeholder = new QWidget();
    placeholder->setMinimumHeight(200);
    setWidget(placeholder);
}