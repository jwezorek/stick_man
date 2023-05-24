#include "tool_settings_pane.h"
#include "util.h"

/*------------------------------------------------------------------------------------------------*/

namespace{

}

ui::tool_settings_pane::tool_settings_pane(QMainWindow* wnd) :
    QDockWidget(tr(""), wnd) {

    setTitleBarWidget( custom_title_bar("tool") );

    
}

void ui::tool_settings_pane::set_tool(QString tool_name, QWidget* contents) {

    ui::set_custom_title_bar_txt(this, tool_name + " tool");
    if (widget()) {
        widget()->hide();
    }
    if (contents && !contents->parentWidget()) {
        contents->setParent(this);
    }
    setWidget(contents);
    if (contents) {
        contents->show();
    }
}