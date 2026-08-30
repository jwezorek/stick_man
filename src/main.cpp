#include "ui/stick_man.h"
#include <QtWidgets/QApplication>
#include <QStyleHints>

int main(int argc, char* argv[])
{
    // The Windows Vista style forces a light palette. The classic Windows
    // style honors Qt's color scheme and system palette, including dark mode.
    QApplication::setStyle("windows");

    QApplication app(argc, argv);
    QGuiApplication::styleHints()->setColorScheme(Qt::ColorScheme::Dark);

    // increase font size for better reading
    QFont defaultFont = QApplication::font();
    defaultFont.setPointSize(defaultFont.pointSize() + 2);
    qApp->setFont(defaultFont);

    ui::stick_man window;
    window.showMaximized();
    return app.exec();
}
