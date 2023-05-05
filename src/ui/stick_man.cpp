#include "stick_man.h"
#include <QtWidgets>

#ifdef Q_OS_WIN
#include <windows.h>
#include <dwmapi.h>
#pragma comment(lib, "Dwmapi.lib")
#endif

/*------------------------------------------------------------------------------------------------*/

namespace {

    void setDarkTitleBar(WId window) {
    #ifdef Q_OS_WIN
        BOOL USE_DARK_MODE = true;
        DwmSetWindowAttribute(
            (HWND)window, DWMWINDOWATTRIBUTE::DWMWA_USE_IMMERSIVE_DARK_MODE,
            &USE_DARK_MODE, sizeof(USE_DARK_MODE));
    #endif
    }

}

ui::stick_man::stick_man(QWidget* parent) :
        QMainWindow(parent), 
        was_shown_(false),
        tool_mgr_(this),
        tool_pal_(new tool_palette(this)),
        prop_pane_(new properties_pane(this)),
        anim_pane_(new animation_pane(this)) {
    setDarkTitleBar(winId());

    setDockNestingEnabled(true);
    addDockWidget(Qt::LeftDockWidgetArea, tool_pal_);
    addDockWidget(Qt::RightDockWidgetArea, prop_pane_);
    addDockWidget(Qt::BottomDockWidgetArea, anim_pane_);

    QScrollArea* scroller = new QScrollArea();
    scroller->setWidget(canvas_ = new ui::canvas());
    scroller->setAlignment(Qt::AlignHCenter | Qt::AlignVCenter);
    setCentralWidget(scroller);
    createMainMenu();
}

void ui::stick_man::showEvent(QShowEvent* event) {
    QMainWindow::showEvent(event);
    was_shown_ = true;
}

void ui::stick_man::resizeEvent(QResizeEvent* event) {
    QMainWindow::resizeEvent(event);
    if (was_shown_ && canvas_->size() == QSize(0, 0)) {
        auto size = this->size();
        auto* layout = this->layout();
        auto marg = layout->contentsMargins();
        QSize client_size = QSize(
            size.width() - marg.left() - marg.right(),
            size.height() - marg.top() - marg.bottom() - menuBar()->height()
        );

        for (QDockWidget* dw : findChildren<QDockWidget*>()) {
            if (dw->isVisible() && !dw->isFloating()) {
                auto area = dockWidgetArea(dw);
                if (area == Qt::LeftDockWidgetArea || area == Qt::RightDockWidgetArea) {
                    client_size.setWidth(client_size.width() - dw->size().width());
                } else {
                    client_size.setHeight(client_size.height() - dw->size().height());
                }
            }
        }

        canvas_->setFixedSize(client_size);
    }
}

void ui::stick_man::open()
{
}

ui::canvas& ui::stick_man::canvas() {
    return *canvas_;
}

ui::tool_manager& ui::stick_man::tool_mgr() {
    return tool_mgr_;
}

void ui::stick_man::createMainMenu()
{
    auto file_menu = menuBar()->addMenu(tr("&File"));
    QAction* actionOpen = new QAction(tr("&Open"), this);
    connect(actionOpen, &QAction::triggered, this, &stick_man::open);
    file_menu->addAction(actionOpen);

    auto view_menu = menuBar()->addMenu(tr("View"));
    QAction* actionNewDockTab = new QAction(tr("foo"), this);
    view_menu->addAction(actionNewDockTab);
}