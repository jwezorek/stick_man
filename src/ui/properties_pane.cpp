#include "properties_pane.h"

/*------------------------------------------------------------------------------------------------*/

namespace{

}

ui::properties_pane::properties_pane(QMainWindow* wnd) :
    QDockWidget(tr("properties"), wnd) {
    auto title_bar = new QLabel("properties");
    title_bar->setPixmap(QPixmap(":/images/tool_palette_thumb.png"));
    title_bar->setStyleSheet(QString("background-color: ") +
        palette().color(QWidget::backgroundRole()).name());
    //setTitleBarWidget(title_bar);

    auto placeholder = new QWidget();
    placeholder->setMinimumWidth(200);
    setWidget(placeholder);
}