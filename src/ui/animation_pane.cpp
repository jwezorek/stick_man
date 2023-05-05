#include "animation_pane.h"

/*------------------------------------------------------------------------------------------------*/

namespace {

}

ui::animation_pane::animation_pane(QMainWindow* wnd) :
        QDockWidget(tr("animation"), wnd) {
    auto title_bar = new QLabel("animation");
    title_bar->setPixmap(QPixmap(":/images/tool_palette_thumb.png"));
    title_bar->setStyleSheet(QString("background-color: ") +
        palette().color(QWidget::backgroundRole()).name());
    //setTitleBarWidget(title_bar);

    auto placeholder = new QWidget();
    placeholder->setMinimumHeight(200);
    setWidget(placeholder);
}