#include "tool_palette.h"
#include "flowlayout.h"
#include <ranges>
#include <vector>
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

    static const std::vector<tool_info> k_tools = { 
        {ui::tool_id::pan, "pan tool", "pan_icon.png"},
        {ui::tool_id::zoom, "zoom tool", "zoom_icon.png"},
        {ui::tool_id::arrow, "arrow tool", "arrow_icon.png"},
        {ui::tool_id::move, "move tool", "move_icon.png"},
        {ui::tool_id::add_joint, "add joint tool", "add_joint_icon.png"},
        {ui::tool_id::add_bone, "add bone tool", "add_bone_icon.png"}
    };

}

namespace ui {

    class tool_btn : public QPushButton {
    private:
        ui::tool_id id_;
        QString bkgd_color_str_;
    public:
        tool_btn(tool_id id, QString icon_rsrc) : id_(id) {
            QIcon icon(QString(":/images/") + icon_rsrc);
            setIcon(icon);
            setIconSize(QSize(32, 32));
            bkgd_color_str_ = palette().color(QWidget::backgroundRole()).name();
            setStyleSheet("tooltip-text-color: white");
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

ui::tool_palette::tool_palette(QMainWindow* wnd) :
        QDockWidget(tr(""), wnd), curr_tool_id_(tool_id::none) {

    auto title_bar = new QLabel("");
    title_bar->setPixmap(QPixmap(":/images/tool_palette_thumb.png"));
    title_bar->setStyleSheet(QString("background-color: ") + 
        palette().color(QWidget::backgroundRole()).name());
    setTitleBarWidget(title_bar);

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

    connect(this, &QDockWidget::topLevelChanged,
        [this]() { 
            if (isFloating()) {
                this->adjustSize();
            }
        }
    );
}

/*
QSize ui::tool_palette::sizeHint() const {
    auto children = findChildren<tool_btn*>();
    auto bounds = QRect(0,0,0,0);
    for (auto btn : children) {
        auto rect = btn->geometry();
        bounds = bounds.united(rect);
    }
    return bounds.size();
}

 QSize ui::tool_palette::minimumSizeHint() const {
     return QWidget::
}
*/

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

