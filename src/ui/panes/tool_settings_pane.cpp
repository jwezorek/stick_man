#include "tool_settings_pane.h"
#include "../tools/tool_manager.h"
#include "../util.h"

/*------------------------------------------------------------------------------------------------*/

namespace{

}

ui::pane::tool_settings::tool_settings(QMainWindow* wnd) :
    QDockWidget(tr(""), wnd) {

    setTitleBarWidget( custom_title_bar("tool") );
}

void ui::pane::tool_settings::set_tool(QString tool_name, QWidget* contents) {

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

void ui::pane::tool_settings::on_current_tool_changed(tool& tool) {
    tool.populate_settings(this);
}

void ui::pane::tool_settings::init(tool_manager& tool_mgr) {
    connect(&tool_mgr, &tool_manager::current_tool_changed,
        this, &tool_settings::on_current_tool_changed
    );
}