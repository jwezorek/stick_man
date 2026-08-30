#include "ui/stick_man.h"
#include <QtWidgets/QApplication>
#include <QStyleHints>
#include <qdebug.h>

int main(int argc, char* argv[])
{
    qDebug() << QT_VERSION_STR;

    QApplication app(argc, argv);
    QGuiApplication::styleHints()->setColorScheme(Qt::ColorScheme::Dark);

    qDebug() << "compile Qt:" << QT_VERSION_STR;
    qDebug() << "runtime Qt:" << qVersion();
    qDebug() << "platform:" << QGuiApplication::platformName();
    qDebug() << "scheme before:"
        << QGuiApplication::styleHints()->colorScheme();

    QGuiApplication::styleHints()->setColorScheme(Qt::ColorScheme::Dark);

    qDebug() << "scheme after:"
        << QGuiApplication::styleHints()->colorScheme();
    qDebug() << "window:"
        << QApplication::palette().color(QPalette::Window);
    qDebug() << "window text:"
        << QApplication::palette().color(QPalette::WindowText);
    qDebug() << "style:"
        << QApplication::style()->objectName();

    // increase font size for better reading
    QFont defaultFont = QApplication::font();
    defaultFont.setPointSize(defaultFont.pointSize() + 2);
    qApp->setFont(defaultFont);

    ui::stick_man window;
    window.showMaximized();
    return app.exec();
}
