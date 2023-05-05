#include "properties_pane.h"
#include "util.h"

/*------------------------------------------------------------------------------------------------*/

namespace{

}

ui::properties_pane::properties_pane(QMainWindow* wnd) :
    QDockWidget(tr(""), wnd) {

    setTitleBarWidget( custom_title_bar("properties") );

    auto placeholder = new QWidget();
    placeholder->setMinimumWidth(200);
    setWidget(placeholder);
}