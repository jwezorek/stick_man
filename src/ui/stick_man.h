#pragma once

#include <QtWidgets/QMainWindow>
#include "canvas.h"
#include "tools.h"
#include "../core/sm_skeleton.h"

/*------------------------------------------------------------------------------------------------*/

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
		void save();
		void save_as();
		void exit();

        void do_undo();
        void do_redo();
        void do_cut();
        void do_copy();
        void do_paste();
        void do_delete();

        void insert_new_tab();

        tool_manager& tool_mgr();
        sm::world& sandbox();
        tool_settings_pane& tool_pane();
		skeleton_pane& skel_pane();
        canvas_manager& canvases();

    private:

        void insert_file_menu();
        void insert_edit_menu();
        void insert_project_menu();
        void insert_view_menu();
        void createMainMenu();
		void showEvent(QShowEvent* event) override;
		void resizeEvent(QResizeEvent* event) override;

        tool_manager tool_mgr_;
        tool_palette* tool_pal_;
        animation_pane* anim_pane_;
        tool_settings_pane* tool_pane_;
		skeleton_pane* skel_pane_;
        canvas_manager* canvases_;
        sm::world world_;
		bool was_shown_;
		bool has_fully_layed_out_widgets_;
    };

}