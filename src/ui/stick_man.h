#pragma once

#include <QtWidgets/QMainWindow>
#include "canvas.h"
#include "tools.h"
#include "../core/ik_sandbox.h"

namespace ui {

    class tool_palette;
    class animation_pane;
    class properties_pane;
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
    private:
        void showEvent(QShowEvent* event) override;
        void resizeEvent(QResizeEvent* event) override;

        void createMainMenu();

        bool was_shown_;
        tool_manager tool_mgr_;
        tool_palette* tool_pal_;
        animation_pane* anim_pane_;
        tool_settings_pane* tool_pane_;
        properties_pane* prop_pane_;
        canvas_view* view_;
        sm::ik_sandbox sandbox_;
    };

}