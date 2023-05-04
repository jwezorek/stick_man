#include "tool_palette.h"
#include "flowlayout.h"
#include <ranges>
#include <array>
#include <tuple>

namespace r = std::ranges;
namespace rv = std::ranges::views;

/*------------------------------------------------------------------------------------------------*/

namespace {

    struct tool_info {
        ui::tool_id id;
        const char* name;
        const char* rsrc_name;
    };

    static const std::array<tool_info, 2> k_tools = {{
        {ui::tool_id::pan, "pan", "pan_icon.png"},
        {ui::tool_id::zoom, "zoom", "zoom_icon.png"}
    }};

}

namespace ui {

    class tool_btn : public QPushButton {
    private:
        ui::tool_id id_;
        QString bkgd_color_str_;
    public:
        tool_btn(tool_id id, QString icon_rsrc) : id_(id) {
            QString rsrc_path = QString(":/images/") + icon_rsrc;
            QIcon icon(rsrc_path);
            setIcon(icon);
            setIconSize(QSize(32, 32));
            bkgd_color_str_ = palette().color(QWidget::backgroundRole()).name();
        }

        void deactivate() {
            setStyleSheet(QString("background-color: ") + bkgd_color_str_);
        }

        void activate() {
            setStyleSheet("background-color: blue");
        }

        tool_id id() const {
            return id_;
        }
    };

}

ui::tool_palette::tool_palette(QMainWindow* wnd, bool vertical) :
        QDockWidget(tr(""), wnd), curr_tool_id_(tool_id::none) {
    setFeatures(QDockWidget::DockWidgetMovable);
    setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    auto layout = new FlowLayout(nullptr, -1,1,0);

    for (const auto& [id, name, rsrc] : k_tools) {
        auto tool = new tool_btn(id, rsrc);
        layout->addWidget(tool);
        connect(tool, &QPushButton::clicked,
            [this, tool]() {handle_tool_click(tool); });
        tool->setToolTip(name);
    }

    auto* widget = new QWidget(this);
    widget->setLayout(layout);
    this->setWidget(widget);
}

void ui::tool_palette::handle_tool_click(tool_btn* btn) {
    if (btn->id() == curr_tool_id_) {
        return;
    }
    if (curr_tool_id_ != tool_id::none) {
        tool_from_id(curr_tool_id_)->deactivate();
    }
    btn->activate();
    curr_tool_id_ = btn->id();
    emit selected_tool_changed(curr_tool_id_);
}

ui::tool_btn* ui::tool_palette::tool_from_id(tool_id id)
{
    if (id == tool_id::none) {
        return nullptr;
    }
    auto tools = this->findChildren<tool_btn*>();
    return *r::find_if(tools,
        [id](auto ptr) {return ptr->id() == id; }
    );
}

bool ui::tool_palette::is_vertical() const {
    return is_vertical_;
}

void ui::tool_palette::set_orientation(bool vert) {

}