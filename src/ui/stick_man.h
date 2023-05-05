#pragma once

#include <QtWidgets/QMainWindow>
#include "animation_pane.h"
#include "tool_palette.h"
#include "properties_pane.h"
#include "canvas.h"

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
        canvas* canvas_;
        tool_palette* tool_pal_;
        animation_pane* anim_pane_;
        properties_pane* prop_pane_;
    };

}