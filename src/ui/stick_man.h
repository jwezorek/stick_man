#pragma once

#include <QtWidgets/QMainWindow>
#include "canvas/scene.h"
#include "tools/tool_manager.h"
#include "animation_controller.h"
#include "../model/project.h"

/*------------------------------------------------------------------------------------------------*/

namespace ui {

    namespace pane {
        class animation;
        class skeleton;
        class tools;
    }

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

        void insert_new_tab();
        void create_animation();
        void create_pose();
        void debug();

        tool::manager& tool_mgr();
        mdl::project& project();
        pane::tool_settings& tool_pane();
		pane::skeleton& skel_pane();
        canvas::manager& canvases();

    private:

        void insert_file_menu();
        void insert_edit_menu();
        void insert_project_menu();
        void insert_view_menu();
        void createMainMenu();
		void showEvent(QShowEvent* event) override;
		void resizeEvent(QResizeEvent* event) override;
        void update_undo_and_redo(bool can_redo, bool can_undo);

        tool::manager tool_mgr_;
        pane::tools* tool_pal_;
        pane::animation* anim_pane_;
        pane::tool_settings* tool_pane_;
		pane::skeleton * skel_pane_;
        canvas::manager* canvases_;
        mdl::project project_;
		bool was_shown_;
		bool has_fully_layed_out_widgets_;
        QAction* undo_action_;
        QAction* redo_action_;

        animation_controller anim_;
    };

}