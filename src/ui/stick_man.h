#pragma once

#include <QtWidgets/QMainWindow>

namespace ui {

    class stick_man : public QMainWindow
    {
        Q_OBJECT

    public:
        stick_man(QWidget* parent = Q_NULLPTR);

        void open();
        void addDockTab();

    private:
        void showEvent(QShowEvent* event) override;
        void resizeEvent(QResizeEvent* event) override;

        void createMainMenu();

        bool was_shown_;
        QWidget* canvas_;
    };

}