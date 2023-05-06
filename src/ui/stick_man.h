#pragma once

#include <QtWidgets/QMainWindow>
#include "animation_pane.h"
#include "tool_palette.h"
#include "properties_pane.h"
#include "canvas.h"
#include "tools.h"

namespace ui {

    class stick_man : public QMainWindow
    {
        Q_OBJECT

    public:
        stick_man(QWidget* parent = Q_NULLPTR);

        void open();
        ui::canvas& canvas();
        tool_manager& tool_mgr();

    private:
        void showEvent(QShowEvent* event) override;
        void resizeEvent(QResizeEvent* event) override;

        void createMainMenu();

        bool was_shown_;
        ui::canvas* canvas_;
        tool_manager tool_mgr_;
        tool_palette* tool_pal_;
        animation_pane* anim_pane_;
        properties_pane* prop_pane_;
        QGraphicsView* view_;
    };

}