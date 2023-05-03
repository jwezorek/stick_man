#include "stick_man.h"
#include "canvas.h"
#include <QtWidgets>

ui::stick_man::stick_man(QWidget* parent) :
    QMainWindow(parent)
{
    setDockNestingEnabled(true);

    QScrollArea* scroller = new QScrollArea();
    scroller->setWidget(canvas_ = new canvas());
    scroller->setAlignment(Qt::AlignHCenter | Qt::AlignVCenter);

    setCentralWidget(scroller);
    createMainMenu();
}

void ui::stick_man::open()
{
}

void ui::stick_man::addDockTab()
{
    QDockWidget* dock = new QDockWidget(tr("foobar"), this);
    auto some_text = new QListWidget(dock);
    some_text->addItems(QStringList()
        << "Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do "
        << "eiusmod tempor incididunt ut labore et dolore magna aliqua. Ut "
        << "enim ad minim veniam, quis nostrud exercitation ullamco "
        << "laboris nisi ut aliquip ex ea commodo consequat. Duis aute irure "
        << "dolor in reprehenderit in voluptate velit esse cillum dolore eu "
        << "fugiat nulla pariatur. Excepteur sint occaecat cupidatat non ");
    dock->setWidget(some_text);
    addDockWidget(Qt::BottomDockWidgetArea, dock);
}

void ui::stick_man::createMainMenu()
{
    auto file_menu = menuBar()->addMenu(tr("&File"));
    QAction* actionOpen = new QAction(tr("&Open"), this);
    connect(actionOpen, &QAction::triggered, this, &stick_man::open);
    file_menu->addAction(actionOpen);

    auto view_menu = menuBar()->addMenu(tr("View"));
    QAction* actionNewDockTab = new QAction(tr("Add Dock Tab"), this);
    connect(actionNewDockTab, &QAction::triggered, this, &stick_man::addDockTab);
    view_menu->addAction(actionNewDockTab);
}