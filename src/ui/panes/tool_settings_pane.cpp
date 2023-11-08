#include "tool_settings_pane.h"
#include "../tools/tools.h"
#include "../util.h"

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

void ui::tool_settings_pane::on_current_tool_changed(abstract_tool& tool) {
    tool.populate_settings(this);
}

void ui::tool_settings_pane::init(tool_manager& tool_mgr) {
    connect(&tool_mgr, &tool_manager::current_tool_changed,
        this, &tool_settings_pane::on_current_tool_changed
    );
}