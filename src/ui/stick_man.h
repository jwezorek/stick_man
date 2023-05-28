#pragma once

#include <QtWidgets/QMainWindow>
#include "canvas.h"
#include "tools.h"
#include "../core/ik_sandbox.h"

namespace ui {

    class tool_palette;
    class animation_pane;
    class skeleton_pane;
    class tool_settings_pane;

    class stick_man : public QMainWindow
    {
        Q_OBJECT

    public:
        stick_man(QWidget* parent = Q_NULLPTR);

        void open();
        tool_manager& tool_mgr();
        canvas_view& view();
        sm::ik_sandbox& sandbox();
        tool_settings_pane& tool_pane();
    private:

        void createMainMenu();

        tool_manager tool_mgr_;
        tool_palette* tool_pal_;
        animation_pane* anim_pane_;
        tool_settings_pane* tool_pane_;
        skeleton_pane* skel_pane_;
        canvas_view* view_;
        sm::ik_sandbox sandbox_;
    };

}