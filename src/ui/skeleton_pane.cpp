#include "skeleton_pane.h"
#include "util.h"

/*------------------------------------------------------------------------------------------------*/

namespace{

}

ui::skeleton_pane::skeleton_pane(QMainWindow* wnd) :
    QDockWidget(tr(""), wnd) {

    setTitleBarWidget( custom_title_bar("skeleton") );

    auto placeholder = new QWidget();
    placeholder->setMinimumWidth(200);
    setWidget(placeholder);
}