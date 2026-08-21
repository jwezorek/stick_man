#include "tool_settings_pane.h"
#include "../tools/tool_manager.h"
#include "../util.h"

/*------------------------------------------------------------------------------------------------*/

namespace{

}

ui::pane::tool_settings::tool_settings(QMainWindow* wnd) :
    QDockWidget(tr(""), wnd),
    scroll_area_(new QScrollArea(this)),
    contents_host_(new QWidget()),
    contents_layout_(new QVBoxLayout(contents_host_)),
    current_contents_(nullptr) {

    setTitleBarWidget(custom_title_bar("tool"));

    contents_layout_->setContentsMargins(0, 0, 0, 0);
    contents_layout_->setAlignment(Qt::AlignTop);

    scroll_area_->setWidgetResizable(true);
    scroll_area_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll_area_->setFrameShape(QFrame::NoFrame);
    scroll_area_->setWidget(contents_host_);
    setWidget(scroll_area_);
}

void ui::pane::tool_settings::set_tool(QString tool_name, QWidget* contents) {

    ui::set_custom_title_bar_txt(this, tool_name + " tool");

    if (current_contents_ == contents) {
        return;
    }

    if (current_contents_) {
        contents_layout_->removeWidget(current_contents_);
        current_contents_->hide();
    }

    current_contents_ = contents;
    if (!current_contents_) {
        return;
    }

    if (current_contents_->parentWidget() != contents_host_) {
        current_contents_->setParent(contents_host_);
    }
    contents_layout_->addWidget(current_contents_);
    current_contents_->show();
}

void ui::pane::tool_settings::on_current_tool_changed(tool::base& tool) {
    tool.populate_settings(this);
}

void ui::pane::tool_settings::init(tool::manager& tool_mgr) {
    connect(&tool_mgr, &tool::manager::current_tool_changed,
        this, &tool_settings::on_current_tool_changed
    );
}