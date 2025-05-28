#include "stick_man.h"
#include "panes/skeleton_pane.h"
#include "panes/animation_pane.h"
#include "panes/tools_pane.h"
#include "panes/tool_settings_pane.h"
#include "canvas/canvas_item.h"
#include "canvas/canvas_manager.h"
#include "tools/tool_manager.h"
#include "tools/zoom_tool.h"
#include "util.h"
#include "clipboard.h"
#include <QtWidgets>
#include <QSettings>

#ifdef Q_OS_WIN
#include <windows.h>
#include <dwmapi.h>
#pragma comment(lib, "Dwmapi.lib")
#endif

//debug
#include "../core/sm_bone.h"
#include "../core/sm_visit.h"
#include "canvas/bone_item.h"
#include "canvas/node_item.h"

/*------------------------------------------------------------------------------------------------*/

namespace r = std::ranges;
namespace rv = std::ranges::views;

namespace {

    void to_do(const std::string& msg) {
        QMessageBox msgBox;
        msgBox.setWindowTitle("TODO");
        msgBox.setText(msg.c_str());
        msgBox.setIcon(QMessageBox::Information);
        msgBox.setStandardButtons(QMessageBox::Ok);
        msgBox.exec();
    }


    void set_dark_titlebar(WId window) {
    #ifdef Q_OS_WIN
        BOOL USE_DARK_MODE = true;
        DwmSetWindowAttribute(
            (HWND)window, DWMWINDOWATTRIBUTE::DWMWA_USE_IMMERSIVE_DARK_MODE,
            &USE_DARK_MODE, sizeof(USE_DARK_MODE));
    #endif
    }

    QString get_last_directory() {
        QSettings settings("adventure_lab", "stick_man");
        return settings.value("lastDirectory", QDir::homePath()).toString();
    }

    void set_last_directory(const QString& path) {
        QSettings settings("adventure_lab", "stick_man");
        settings.setValue("lastDirectory", path);
    }
}

ui::stick_man::stick_man(QWidget* parent) :
		QMainWindow(parent),
		was_shown_(false),
		has_fully_layed_out_widgets_(false),
		tool_pal_(new pane::tools(this)),
		anim_pane_(new pane::animation(this)),
		tool_pane_(new pane::tool_settings(this)),
		skel_pane_(new pane::skeleton(this)) {
    set_dark_titlebar(winId());

    setDockNestingEnabled(true);
    addDockWidget(Qt::LeftDockWidgetArea, tool_pal_);
    addDockWidget(Qt::RightDockWidgetArea, tool_pane_);
    addDockWidget(Qt::RightDockWidgetArea, skel_pane_);
    addDockWidget(Qt::BottomDockWidgetArea, anim_pane_);

    setCentralWidget(canvases_ = new canvas::manager(tool_mgr_));
    createMainMenu();
    canvases_->init(project_);
	skel_pane_->init(*canvases_, project_);
	tool_mgr_.init(*canvases_, project_);
    tool_pane_->init(tool_mgr_);
}

void ui::stick_man::open()
{
    auto dir = get_last_directory();
	auto path = QFileDialog::getOpenFileName(
		this, "Open stick man", dir, "stick man JSON (*.smj);;All Files (*)");

	if (!path.isEmpty()) {
        set_last_directory(QFileInfo(path).absolutePath());
		QFile file(path);
		if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
			QTextStream in(&file);
			QString content = in.readAll();
			file.close();

            auto success = project_.from_json(content.toStdString()); 
            if (!success) {
                QMessageBox::critical(this, "Error", "Error opening file.");
            }
		} else {
			QMessageBox::critical(this, "Error", "Could not open file.");
		}
	}
}

void ui::stick_man::save() {
    to_do("save");
}

void ui::stick_man::save_as() {
    auto dir = get_last_directory();
	QString path = QFileDialog::getSaveFileName(
		this, "Save stick man As", dir, "stick man JSON (*.smj);;All Files (*)");

	if (! path.isEmpty()) {
        set_last_directory(QFileInfo(path).absolutePath());
		// Perform the actual save operation
		QFile file(path);
		if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
			QTextStream out(&file);
			out << project_.to_json().c_str();
			file.close();
		} else {
			QMessageBox::critical(this, "Error", "Bad pathname.");
		}
	}
}

void ui::stick_man::exit() {
	bool unsavedChanges = false; // TODO

	if (unsavedChanges) {

		QMessageBox::StandardButton response = QMessageBox::question(
			this, "Unsaved Changes",
			"You have unsaved changes. Do you want to save them before quitting?",
			QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);

		if (response == QMessageBox::Save) {
			save();
		}
		else if (response == QMessageBox::Discard) {

		} else {
			return;
		}
	}

	QCoreApplication::quit();
}

void ui::stick_man::debug() {
}

void ui::stick_man::insert_new_tab() {
    auto valid_tab_name = [this](const std::string& str)->bool {
            return !project_.has_tab(str);
        };
    auto new_tab_name = query_for_valid_string(this, valid_tab_name, "New tab", "New tab name");
    if (new_tab_name.empty()) {
        return;
    }
    project_.add_new_tab(new_tab_name);
}

void ui::stick_man::create_animation() {
    to_do("create animation");
}

void ui::stick_man::create_pose()
{
    to_do("create pose");
}


ui::tool::manager& ui::stick_man::tool_mgr() {
    return tool_mgr_;
}

mdl::project& ui::stick_man::project() {
    return project_;
}

ui::pane::tool_settings& ui::stick_man::tool_pane() {
    return *tool_pane_;
}


ui::pane::skeleton& ui::stick_man::skel_pane() {
	return *skel_pane_;
}

ui::canvas::manager& ui::stick_man::canvases() {
    return *canvases_;
}

void ui::stick_man::insert_file_menu() {
    auto file_menu = menuBar()->addMenu(tr("&File"));
    QAction* actionOpen = new QAction(tr("Open stick man"), this);
    QAction* actionSave = new QAction(tr("Save stick man"), this);
    QAction* actionSaveAs = new QAction(tr("Save as..."), this);
    QAction* actionExit = new QAction(tr("Exit"), this);

    file_menu->addAction(actionOpen);
    file_menu->addAction(actionSave);
    file_menu->addAction(actionSaveAs);
    file_menu->addSeparator();
    file_menu->addAction(actionExit);

    QFontMetrics metrics(file_menu->font());
    int maxWidth = metrics.horizontalAdvance(actionSaveAs->text()) + 20;
    file_menu->setMinimumWidth(maxWidth);

    connect(actionOpen, &QAction::triggered, this, &stick_man::open);
    connect(actionSave, &QAction::triggered, this, &stick_man::save);
    connect(actionSaveAs, &QAction::triggered, this, &stick_man::save_as);
    connect(actionExit, &QAction::triggered, this, &stick_man::exit);
}

void ui::stick_man::do_undo() {
    project_.undo();
}

void ui::stick_man::do_redo() {
    project_.redo();
}

void ui::stick_man::insert_edit_menu() {

    undo_action_ = new QAction("Undo", this);
    undo_action_->setShortcut(QKeySequence::Undo);
    connect(undo_action_, &QAction::triggered, this, &stick_man::do_undo);

    redo_action_ = new QAction("Redo", this);
    redo_action_->setShortcut(QKeySequence::Redo);
    connect(redo_action_, &QAction::triggered, this, &stick_man::do_redo);

    QAction* cut_action = new QAction("Cut", this);
    cut_action->setShortcut(QKeySequence::Cut);
    connect(cut_action, &QAction::triggered, 
        [this]() {clipboard::cut(*this); });

    QAction* copy_action = new QAction("Copy", this);
    copy_action->setShortcut(QKeySequence::Copy);
    connect(copy_action, &QAction::triggered,
        [this]() {clipboard::copy(*this); });

    QAction* paste_action = new QAction("Paste", this);
    paste_action->setShortcut(QKeySequence::Paste);
    connect(paste_action, &QAction::triggered,
        [this]() {clipboard::paste(*this, false); });

    QAction* paste_in_place_action = new QAction("Paste in place", this);
    paste_in_place_action->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_V));
    connect(paste_in_place_action, &QAction::triggered,
        [this]() {clipboard::paste(*this, true); });

    QAction* delete_action = new QAction("Delete", this);
    delete_action->setShortcut(QKeySequence::Delete);
    connect(delete_action, &QAction::triggered,
        [this]() {clipboard::del(*this); });

    QMenu* edit_menu = menuBar()->addMenu("Edit");

    edit_menu->addAction(undo_action_);
    edit_menu->addAction(redo_action_);
    edit_menu->addSeparator();
    edit_menu->addAction(cut_action);
    edit_menu->addAction(copy_action);
    edit_menu->addAction(paste_action);
    edit_menu->addAction(paste_in_place_action);
    edit_menu->addSeparator();
    edit_menu->addAction(delete_action);

    connect(&project_, &mdl::project::refresh_undo_redo_state, this, &stick_man::update_undo_and_redo);
    redo_action_->setEnabled(false);
    undo_action_->setEnabled(false);
}

void ui::stick_man::update_undo_and_redo(bool can_redo, bool can_undo) {
    redo_action_->setEnabled(can_redo);
    undo_action_->setEnabled(can_undo);
}

void ui::stick_man::insert_view_menu() {
    auto view_menu = menuBar()->addMenu(tr("View"));
    QMenu* magnification_menu = view_menu->addMenu(tr("Magnification"));

    // Create an action group to make the actions mutually exclusive (like radio buttons)
    QActionGroup* magnification_group = new QActionGroup(this);
    magnification_group->setExclusive(true);

    const auto* zoom_tool = static_cast<const tool::zoom*>(
        &tool_mgr_.tool_from_id(tool::id::zoom)
    );

    // Create actions for each magnification level
    auto zoom_levels = zoom_tool->magnification_levels();
    for (auto level : zoom_levels) {
        auto level_str = std::to_string(level) + "%";
        QAction* action = new QAction(level_str.c_str(), this);
        action->setCheckable(true);
        if (level == 100) {
            action->setChecked(true);  // Set default magnification to 100%
        }
        magnification_group->addAction(action);
        magnification_menu->addAction(action);

        // Connect each action to a slot if you want to handle magnification changes
        connect(action, &QAction::triggered, this, [=]() {
            double scale = level / 100.0;
            zoom_tool->do_zoom(scale);
        });
    }
}

void ui::stick_man::insert_project_menu() {
    auto project_menu = menuBar()->addMenu(tr("Stick Man"));

    auto* new_tab_action = new QAction(tr("Insert new canvas"), this);
    auto* new_animation = new QAction("Create new animation", this);
    auto* new_pose = new QAction("Create new pose", this);

    project_menu->addAction(new_tab_action);
    project_menu->addAction(new_animation);
    project_menu->addAction(new_pose);

    connect(new_tab_action, &QAction::triggered, this, &stick_man::insert_new_tab);
    connect(new_animation, &QAction::triggered, this, &stick_man::create_animation);
    connect(new_pose, &QAction::triggered, this, &stick_man::create_pose);
}

void ui::stick_man::createMainMenu()
{
    insert_file_menu();
    insert_edit_menu();
    insert_project_menu();
    insert_view_menu();

    QString styleSheet =
        "QMenu::separator { background-color: #7f7f7f; color: gray; }"
        "QMenu::item:disabled{ color: gray; background-color: #353535 }";
    menuBar()->setStyleSheet(styleSheet);

}

void ui::stick_man::showEvent(QShowEvent* event) {
	QMainWindow::showEvent(event);
	was_shown_ = true;
}

void ui::stick_man::resizeEvent(QResizeEvent* event) {
	QMainWindow::resizeEvent(event);
	if (was_shown_ && !has_fully_layed_out_widgets_) {
        canvases_->center_active_view();
		has_fully_layed_out_widgets_ = true;
	}
}