#include "ui/stick_man.h"

#include <QtWidgets/QApplication>
#include <QPalette>
#include <QStyleHints>

namespace {

void applyStickManPalette()
{
    // Keep the color choices from the original custom dark theme, but let
    // Qt's Windows style and color-scheme support handle widget rendering
    // and native window decorations.
    QPalette darkPalette;

    darkPalette.setColor(QPalette::Window, QColor(53, 53, 53));
    darkPalette.setColor(QPalette::WindowText, Qt::white);
    darkPalette.setColor(QPalette::Disabled, QPalette::WindowText, QColor(127, 127, 127));

    darkPalette.setColor(QPalette::Base, QColor(42, 42, 42));
    darkPalette.setColor(QPalette::AlternateBase, QColor(66, 66, 66));

    darkPalette.setColor(QPalette::ToolTipBase, Qt::white);
    darkPalette.setColor(QPalette::ToolTipText, Qt::white);

    darkPalette.setColor(QPalette::Text, Qt::white);
    darkPalette.setColor(QPalette::Disabled, QPalette::Text, QColor(127, 127, 127));

    darkPalette.setColor(QPalette::Dark, QColor(35, 35, 35));
    darkPalette.setColor(QPalette::Shadow, QColor(20, 20, 20));

    darkPalette.setColor(QPalette::Button, QColor(53, 53, 53));
    darkPalette.setColor(QPalette::ButtonText, Qt::white);
    darkPalette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(127, 127, 127));

    darkPalette.setColor(QPalette::BrightText, Qt::red);
    darkPalette.setColor(QPalette::Link, QColor(42, 130, 218));

    darkPalette.setColor(QPalette::Highlight, QColor(42, 130, 218));
    darkPalette.setColor(QPalette::Disabled, QPalette::Highlight, QColor(80, 80, 80));
    darkPalette.setColor(QPalette::HighlightedText, Qt::white);
    darkPalette.setColor(QPalette::Disabled, QPalette::HighlightedText, QColor(127, 127, 127));

    qApp->setPalette(darkPalette);
}

} // namespace

int main(int argc, char* argv[])
{
    // The Windows style honors Qt's dark color scheme while still allowing
    // the application to supply its preferred widget palette.
    QApplication::setStyle("windows");

    QApplication app(argc, argv);

    // Let Qt/Windows handle native dark-mode behavior, including title bars.
    QGuiApplication::styleHints()->setColorScheme(Qt::ColorScheme::Dark);

    applyStickManPalette();

    // Increase font size for better reading.
    QFont defaultFont = QApplication::font();
    defaultFont.setPointSize(defaultFont.pointSize() + 2);
    qApp->setFont(defaultFont);

    ui::stick_man window;
    window.showMaximized();

    return app.exec();
}
