#include "tool_settings_pane.h"
#include "util.h"

/*------------------------------------------------------------------------------------------------*/

namespace{

}

ui::tool_settings_pane::tool_settings_pane(QMainWindow* wnd) :
    QDockWidget(tr(""), wnd) {

    setTitleBarWidget( custom_title_bar("tool settings") );

    auto placeholder = new QWidget();
    placeholder->setMinimumWidth(200);
    setWidget(placeholder);
}