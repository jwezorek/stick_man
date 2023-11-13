#include "tools_pane.h"
#include "../stick_man.h"
#include "../util.h"
#include <ranges>
#include <vector>
#include <tuple>

namespace r = std::ranges;
namespace rv = std::ranges::views;

/*------------------------------------------------------------------------------------------------*/

namespace ui {

    class tool_btn : public QPushButton {
    private:
        ui::tool::id id_;
        QString bkgd_color_str_;
    public:
        tool_btn(tool::id id, QString icon_rsrc) : id_(id) {
            QIcon icon(QString(":/images/") + icon_rsrc);
            setIcon(icon);
            setIconSize(QSize(32, 32));
            setFixedSize(QSize(42, 42));
            bkgd_color_str_ = palette().color(QWidget::backgroundRole()).name();
            setStyleSheet("QToolTip {  background-color: black; color: white; border: black solid 1px}");
        }

        void deactivate() {
            setStyleSheet("background-color: " + bkgd_color_str_);
        }

        void activate() {
			setStyleSheet("background-color: " + k_accent_color.name());
        }

        tool::id id() const {
            return id_;
        }
    };

}

ui::pane::tools::tools(QMainWindow* wnd) :
        QDockWidget(tr(""), wnd),
        tools_(static_cast<stick_man*>(wnd)->tool_mgr()) {

    auto title_bar = new QLabel("");
    title_bar->setPixmap(QPixmap(":/images/tool_palette_thumb.png"));
    title_bar->setStyleSheet(QString("background-color: ") + 
        palette().color(QWidget::backgroundRole()).name());
    setTitleBarWidget(title_bar);

    auto layout = new ui::FlowLayout(nullptr, -1,1,0);

    for (const auto& [id, name, rsrc] : tools_.tool_info()) {
        auto tool = new tool_btn(id, rsrc);
        layout->addWidget(tool);
        connect(tool, &QPushButton::clicked,
            [wnd, this, tool]() {
                handle_tool_click(static_cast<stick_man*>(wnd)->canvases(), tool);
            }
        );
        tool->setToolTip(name);
    }

    auto* widget = new QWidget(this);
    widget->setLayout(layout);
    this->setWidget(widget);

    connect(this, &QDockWidget::topLevelChanged,
        [this]() { 
            if (isFloating()) {
                this->adjustSize();
            }
        }
    );
}

ui::tool_btn* ui::pane::tools::tool_from_id(tool::id id)
{
    if (id == tool::id::none) {
        return nullptr;
    }
    auto tools = this->findChildren<tool_btn*>();
    return *r::find_if(tools,
        [id](auto ptr) {return ptr->id() == id; }
    );
}

void ui::pane::tools::handle_tool_click(canvas::manager& canvases, tool_btn* btn) {

    tool::id current_tool_id = (tools_.has_current_tool()) ?
        tools_.current_tool().id() : tool::id::none;

    if (btn->id() == current_tool_id) {
        return;
    }
    
    if (current_tool_id != tool::id::none) {
        tool_from_id(current_tool_id)->deactivate();
    }

    btn->activate();
    tools_.set_current_tool(canvases, btn->id() );
}

