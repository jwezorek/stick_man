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

    setCentralWidget(view_ = new canvas_view());
    createMainMenu();
}

void ui::stick_man::showEvent(QShowEvent* event) {
    QMainWindow::showEvent(event);
    was_shown_ = true;
}

void ui::stick_man::resizeEvent(QResizeEvent* event) {
    QMainWindow::resizeEvent(event);
    
    if (was_shown_ ) {
    }
}

void ui::stick_man::open()
{
}

ui::tool_manager& ui::stick_man::tool_mgr() {
    return tool_mgr_;
}

ui::canvas_view& ui::stick_man::view() {
    return *view_;
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