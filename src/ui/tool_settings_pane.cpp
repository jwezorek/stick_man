#include "tool_settings_pane.h"
#include "util.h"

/*------------------------------------------------------------------------------------------------*/

namespace{

}

ui::tool_settings_pane::tool_settings_pane(QMainWindow* wnd) :
    QDockWidget(tr(""), wnd) {

    setTitleBarWidget( custom_title_bar("tool") );

    auto contents_widget = new QWidget();
    setWidget(contents_widget);
    contents_widget->setLayout(contents_ = new FlowLayout());
}


QLayout* ui::tool_settings_pane::contents() {
    return contents_;
}