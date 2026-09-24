#pragma once
#include <QtWidgets/QMainWindow>
#include "canvas/scene.hpp"
#include "tools/tool_manager.hpp"
#include "../model/project.hpp"

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
        ~stick_man() override;
        void new_file();
        void open();
        bool save();
        bool save_as();
        void exit();

        void do_undo();
        void do_redo();

        void create_animation();
        void create_pose();
        void debug();

        tool::manager& tool_mgr();
        mdl::project& project();
        pane::tool_settings& tool_pane();
        pane::skeleton& skel_pane();
        canvas::manager& canvases();

    private:
        enum class save_decision { proceed, cancel };

        void insert_file_menu();
        void insert_edit_menu();
        void insert_project_menu();
        void insert_view_menu();
        void reset_view();
        void createMainMenu();
        void showEvent(QShowEvent* event) override;
        void resizeEvent(QResizeEvent* event) override;
        void closeEvent(QCloseEvent* event) override;
        void update_undo_and_redo(bool can_redo, bool can_undo);
        void set_current_file(const QString& file_path);
        void update_window_title();
        save_decision maybe_save_changes();
        bool write_project_file(const QString& file_path);
        tool::manager tool_mgr_;
        pane::tools* tool_pal_;
        pane::animation* anim_pane_;
        pane::tool_settings* tool_pane_;
        pane::skeleton * skel_pane_;
        canvas::manager* canvases_;
        mdl::project project_;
        QString current_file_path_;
        bool was_shown_;
        bool has_fully_layed_out_widgets_;
        QAction* undo_action_;
        QAction* redo_action_;
        QAction* show_constraints_action_ = nullptr;
        QAction* skeleton_visible_action_ = nullptr;
    };

}
