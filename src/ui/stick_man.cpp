#include "stick_man.h"
#include "skeleton_pane.h"
#include "animation_pane.h"
#include "tool_palette.h"
#include "tool_settings_pane.h"
#include "canvas_item.h"
#include "util.h"
#include <QtWidgets>

#ifdef Q_OS_WIN
#include <windows.h>
#include <dwmapi.h>
#pragma comment(lib, "Dwmapi.lib")
#endif

/*------------------------------------------------------------------------------------------------*/

namespace {

    void setDarkTitleBar(WId window) {
    #ifdef Q_OS_WIN
        BOOL USE_DARK_MODE = true;
        DwmSetWindowAttribute(
            (HWND)window, DWMWINDOWATTRIBUTE::DWMWA_USE_IMMERSIVE_DARK_MODE,
            &USE_DARK_MODE, sizeof(USE_DARK_MODE));
    #endif
    }
}

ui::stick_man::stick_man(QWidget* parent) :
		QMainWindow(parent),
		was_shown_(false),
		has_fully_layed_out_widgets_(false),
		tool_mgr_(this),
		tool_pal_(new tool_palette(this)),
		anim_pane_(new animation_pane(this)),
		tool_pane_(new tool_settings_pane(this)),
		skel_pane_(new skeleton_pane(this)) {
    setDarkTitleBar(winId());

    setDockNestingEnabled(true);
    addDockWidget(Qt::LeftDockWidgetArea, tool_pal_);
    addDockWidget(Qt::RightDockWidgetArea, tool_pane_);
    addDockWidget(Qt::RightDockWidgetArea, skel_pane_);
    addDockWidget(Qt::BottomDockWidgetArea, anim_pane_);

    setCentralWidget(canvases_ = new canvas_manager(tool_mgr_));
    createMainMenu();
	skel_pane_->init();
	tool_mgr_.init();
    tool_pane_->init(tool_mgr_);
}

void ui::stick_man::open()
{
	QString filePath = QFileDialog::getOpenFileName(
		this, "Open stick man", QDir::homePath(), "stick man JSON (*.smj);;All Files (*)");

	if (!filePath.isEmpty()) {
		QFile file(filePath);
		if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
			QTextStream in(&file);
			QString content = in.readAll();
			file.close();

            world_.clear();
			world_.from_json(content.toStdString());
			canvases().sync_to_model(world_);
		} else {
			QMessageBox::critical(this, "Error", "Could not open file.");
		}
	}
}

void ui::stick_man::save() {
	
}

void ui::stick_man::save_as() {
	QString filePath = QFileDialog::getSaveFileName(
		this, "Save stick man As", QDir::homePath(), "stick man JSON (*.smj);;All Files (*)");

	if (!filePath.isEmpty()) {
		// Perform the actual save operation
		QFile file(filePath);
		if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
			QTextStream out(&file);
			out << world_.to_json().c_str();
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
			// User cancelled quitting
			return;
		}
	}

	// Quit the application
	QCoreApplication::quit();
}

void ui::stick_man::insert_new_tab() {
    auto tab_names = canvases_->tab_names();
    auto new_name = make_unique_name(tab_names, "untitled");
    canvases_->add_new_tab(new_name.c_str());
}

ui::tool_manager& ui::stick_man::tool_mgr() {
    return tool_mgr_;
}

sm::world& ui::stick_man::sandbox() {
    return world_;
}

ui::tool_settings_pane& ui::stick_man::tool_pane() {
    return *tool_pane_;
}


ui::skeleton_pane& ui::stick_man::skel_pane() {
	return *skel_pane_;
}

ui::canvas_manager& ui::stick_man::canvases() {
    return *canvases_;
}

void ui::stick_man::createMainMenu()
{
    auto file_menu = menuBar()->addMenu(tr("&File"));
    QAction* actionOpen  = new QAction(tr("Open stick man"), this);
	QAction* actionSave  = new QAction(tr("Save stick man"), this);
	QAction* actionSaveAs = new QAction(tr("Save as..."), this);
	QAction* actionExit  = new QAction(tr("Exit"), this);

    file_menu->addAction(actionOpen  );
	file_menu->addAction(actionSave  );
	file_menu->addAction(actionSaveAs);
	file_menu->addSeparator();
	file_menu->addAction(actionExit  );

	QFontMetrics metrics(file_menu->font());
	int maxWidth = metrics.horizontalAdvance(actionSaveAs->text()) + 20; 
	file_menu->setMinimumWidth(maxWidth);

	connect(actionOpen  , &QAction::triggered, this, &stick_man::open  );
	connect(actionSave  , &QAction::triggered, this, &stick_man::save  );
	connect(actionSaveAs, &QAction::triggered, this, &stick_man::save_as);
	connect(actionExit  , &QAction::triggered, this, &stick_man::exit  );

    auto edit_menu = menuBar()->addMenu(tr("Edit"));
    QAction* edit_action = new QAction(tr("foo"), this);
    edit_menu->addAction(edit_action);

    auto project_menu = menuBar()->addMenu(tr("Project"));
    QAction* new_tab_action = new QAction(tr("Insert new skeleton tab"), this);
    project_menu->addAction(new_tab_action);
    connect(new_tab_action, &QAction::triggered, this, &stick_man::insert_new_tab);

    auto view_menu = menuBar()->addMenu(tr("View"));
    QAction* view_action = new QAction(tr("foo"), this);
    view_menu->addAction(view_action);
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