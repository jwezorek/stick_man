#include "tool_palette.h"
#include "flowlayout.h"
#include <ranges>

namespace r = std::ranges;
namespace rv = std::ranges::views;

/*------------------------------------------------------------------------------------------------*/

namespace {
    QPushButton* create_tool(int dim) {
        auto tool = new QPushButton();
        tool->setFixedSize(dim, dim);
        tool->setText("foo");
        return tool;
    }

}

ui::tool_palette::tool_palette(QMainWindow* wnd, bool vertical) :
        QDockWidget(tr("tools"), wnd) {
    setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    auto layout = new FlowLayout();
    layout->setSpacing(1);
    //layout->setAlignment(Qt::AlignTop);

    for (auto i : rv::iota(0, 10)) {
        layout->addWidget(create_tool(k_tool_dim));
    }
    auto* widget = new QWidget(this);
    widget->setLayout(layout);
    this->setWidget(widget);
}

bool ui::tool_palette::is_vertical() const {
    return is_vertical_;
}

void ui::tool_palette::set_orientation(bool vert) {

}