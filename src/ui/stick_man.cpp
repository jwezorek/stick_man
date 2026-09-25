#include "stick_man.hpp"
#include "panes/artwork_browser.hpp"
#include "canvas/artwork_layer.hpp"
#include "panes/skeleton_pane.hpp"
#include "panes/animation_pane.hpp"
#include "panes/tools_pane.hpp"
#include "panes/tool_settings_pane.hpp"
#include "canvas/canvas_item.hpp"
#include "canvas/canvas_manager.hpp"
#include "tools/tool_manager.hpp"
#include "tools/zoom_tool.hpp"
#include "util.hpp"
#include "clipboard.hpp"
#include <QtWidgets>
#include <QFileInfo>
#include <QSaveFile>
#include <QSettings>
#include <cstdint>
#include <ranges>
#include <span>
#include <utility>
#ifdef Q_OS_WIN
#include <windows.h>
#include <dwmapi.h>
#pragma comment(lib, "Dwmapi.lib")
#endif
//debug
#include "../core/sm_bone.hpp"
#include "../core/sm_visit.hpp"
#include "canvas/bone_item.hpp"
#include "canvas/node_item.hpp"

/*------------------------------------------------------------------------------------------------*/

namespace r = std::ranges;
namespace rv = std::ranges::views;
namespace {
    constexpr int layout_state_version = 1;
    constexpr auto layout_settings_organization = "jwezorek";
    constexpr auto layout_settings_application = "stick_man";
    constexpr auto layout_settings_key = "main_window/state";
    void setDarkTitleBar(WId window) {
    #ifdef Q_OS_WIN
        BOOL USE_DARK_MODE = true;
        DwmSetWindowAttribute(
            (HWND)window, DWMWINDOWATTRIBUTE::DWMWA_USE_IMMERSIVE_DARK_MODE,
            &USE_DARK_MODE, sizeof(USE_DARK_MODE));
    #endif
    }

    QString project_result_message(sm::project_result result) {
        switch (result) {
        case sm::project_result::success:
            return {};
        case sm::project_result::invalid_archive:
            return QStringLiteral("The file is not a valid stick_man project archive.");
        case sm::project_result::missing_project_json:
            return QStringLiteral("The project package is missing project.json.");
        case sm::project_result::invalid_project_json:
            return QStringLiteral("The project data is invalid or uses an unsupported format.");
        case sm::project_result::duplicate_object_id:
            return QStringLiteral("The project contains duplicate object IDs.");
        case sm::project_result::archive_error:
            return QStringLiteral("The project package could not be read or written completely.");
        case sm::project_result::invalid_artwork:
            return QStringLiteral("The project contains invalid or unreadable artwork resources.");
        }
        return QStringLiteral("An unknown project error occurred.");
    }

    QString file_operation_message(QString operation, const QString& file_path, QString detail) {
        return QStringLiteral("%1 failed for:\n%2\n\n%3")
            .arg(std::move(operation), QDir::toNativeSeparators(file_path), std::move(detail));
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
    setDarkTitleBar(winId());
    setDockNestingEnabled(true);

    tool_pal_->setObjectName("tools_toolbar");
    tool_pane_->setObjectName("tool_settings_pane");
    skel_pane_->setObjectName("skeleton_pane");
    anim_pane_->setObjectName("animation_pane");

    addToolBar(Qt::LeftToolBarArea, tool_pal_);
    addDockWidget(Qt::RightDockWidgetArea, tool_pane_);
    addDockWidget(Qt::RightDockWidgetArea, skel_pane_);
    addDockWidget(Qt::RightDockWidgetArea, anim_pane_);
    auto* center = new QWidget(this);
    auto* center_layout = new QVBoxLayout(center);
    center_layout->setContentsMargins(0, 0, 0, 0);
    center_layout->addWidget(canvases_ = new canvas::manager(tool_mgr_));
    setCentralWidget(center);
    update_window_title();
    project_.set_topology_edit_confirmation([this](const sm::topology_edit_effects& effects) {
        const auto count = effects.removed_animation_actions.size();
        const auto message = count == 1
            ? QStringLiteral(
                "This edit will also delete 1 animation action that depends on a node, bone, or skeleton being removed.\n\nContinue?")
            : QStringLiteral(
                "This edit will also delete %1 animation actions that depend on nodes, bones, or skeletons being removed.\n\nContinue?")
                .arg(count);
        return QMessageBox::question(this, QStringLiteral("Delete Animation Actions"), message,
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes;
    });
    canvases_->init(project_);

    auto* artwork_browser = new pane::artwork_browser(project_, *canvases_, this);
    addDockWidget(Qt::RightDockWidgetArea, artwork_browser);
    tabifyDockWidget(skel_pane_, artwork_browser);
    tabifyDockWidget(skel_pane_, anim_pane_);

    QSettings settings(layout_settings_organization, layout_settings_application);
    const auto saved_layout = settings.value(layout_settings_key).toByteArray();
    if (saved_layout.isEmpty() || !restoreState(saved_layout, layout_state_version)) {
        skel_pane_->raise();
    }

    connect(qApp, &QCoreApplication::aboutToQuit, this, [this] {
        QSettings settings(layout_settings_organization, layout_settings_application);
        settings.setValue(layout_settings_key, saveState(layout_state_version));
    });

    createMainMenu();
    connect(&project_, &mdl::project::dirty_changed, this, [this](bool) { update_window_title(); });
    skel_pane_->init(*canvases_, project_);
    anim_pane_->init(*canvases_, project_);
    tool_mgr_.init(*canvases_, project_);
    tool_pane_->init(tool_mgr_);
}
void ui::stick_man::set_current_file(const QString& file_path) {
    current_file_path_ = file_path;
    const auto file_name = file_path.isEmpty()
        ? QStringLiteral("untitled")
        : QFileInfo(file_path).fileName();
    canvases_->set_canvas_name(file_name.toStdString());
    update_window_title();
}

void ui::stick_man::update_window_title() {
    const auto file_name = current_file_path_.isEmpty()
        ? QStringLiteral("untitled")
        : QFileInfo(current_file_path_).fileName();
    setWindowTitle(QStringLiteral("stick_man - %1%2")
        .arg(file_name, project_.is_dirty() ? QStringLiteral(" *") : QString{}));
}

ui::stick_man::save_decision ui::stick_man::maybe_save_changes() {
    if (!project_.is_dirty()) {
        return save_decision::proceed;
    }

    const auto response = QMessageBox::warning(
        this,
        QStringLiteral("Unsaved Changes"),
        QStringLiteral("The current project has unsaved changes. Save them before continuing?"),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
        QMessageBox::Save);

    if (response == QMessageBox::Discard) {
        return save_decision::proceed;
    }
    if (response == QMessageBox::Save && save()) {
        return save_decision::proceed;
    }
    return save_decision::cancel;
}

bool ui::stick_man::write_project_file(const QString& file_path) {
    auto serialized = project_.serialize();
    if (!serialized) {
        QMessageBox::critical(this, QStringLiteral("Save Project"),
            file_operation_message(QStringLiteral("Saving"), file_path,
                project_result_message(serialized.error())));
        return false;
    }

    QSaveFile file(file_path);
    if (!file.open(QIODevice::WriteOnly)) {
        QMessageBox::critical(this, QStringLiteral("Save Project"),
            file_operation_message(QStringLiteral("Saving"), file_path, file.errorString()));
        return false;
    }

    const auto& buffer = *serialized;
    const auto written = file.write(
        reinterpret_cast<const char*>(buffer.data()),
        static_cast<qint64>(buffer.size()));
    if (written != static_cast<qint64>(buffer.size())) {
        const auto detail = file.errorString().isEmpty()
            ? QStringLiteral("The complete project could not be written.")
            : file.errorString();
        file.cancelWriting();
        QMessageBox::critical(this, QStringLiteral("Save Project"),
            file_operation_message(QStringLiteral("Saving"), file_path, detail));
        return false;
    }

    if (!file.commit()) {
        QMessageBox::critical(this, QStringLiteral("Save Project"),
            file_operation_message(QStringLiteral("Saving"), file_path, file.errorString()));
        return false;
    }
    return true;
}

void ui::stick_man::new_file() {
    if (maybe_save_changes() == save_decision::cancel) {
        return;
    }

    anim_pane_->leave_animation();
    project_.new_document();
    set_current_file(QString{});
}

void ui::stick_man::open() {
    if (maybe_save_changes() == save_decision::cancel) {
        return;
    }

    const QString file_path = QFileDialog::getOpenFileName(
        this, QStringLiteral("Open stick_man project"), QDir::homePath(),
        QStringLiteral("stick_man Project (*.stickman)"));
    if (file_path.isEmpty()) {
        return;
    }

    QFile file(file_path);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::critical(this, QStringLiteral("Open Project"),
            file_operation_message(QStringLiteral("Opening"), file_path, file.errorString()));
        return;
    }

    const QByteArray content = file.readAll();
    if (file.error() != QFileDevice::NoError) {
        QMessageBox::critical(this, QStringLiteral("Open Project"),
            file_operation_message(QStringLiteral("Reading"), file_path, file.errorString()));
        return;
    }

    const auto* first = reinterpret_cast<const std::uint8_t*>(content.constData());
    const std::span<const std::uint8_t> buffer(first, static_cast<std::size_t>(content.size()));

    // Validate before leaving Animation Mode so a bad package changes neither the
    // current project nor the current editing session. Core loading itself is also
    // transactional, so the live project is replaced only after successful parsing.
    const auto validation = mdl::project::validate_serialized(buffer);
    if (validation != sm::project_result::success) {
        QMessageBox::critical(this, QStringLiteral("Open Project"),
            file_operation_message(QStringLiteral("Opening"), file_path,
                project_result_message(validation)));
        return;
    }

    anim_pane_->leave_animation();
    const auto result = project_.deserialize_result(buffer);
    if (result != sm::project_result::success) {
        QMessageBox::critical(this, QStringLiteral("Open Project"),
            file_operation_message(QStringLiteral("Opening"), file_path,
                project_result_message(result)));
        return;
    }
    set_current_file(file_path);
}

bool ui::stick_man::save() {
    if (current_file_path_.isEmpty()) {
        return save_as();
    }
    if (!write_project_file(current_file_path_)) {
        return false;
    }
    project_.mark_saved();
    return true;
}

bool ui::stick_man::save_as() {
    const auto initial_dir = current_file_path_.isEmpty()
        ? QDir::homePath()
        : QFileInfo(current_file_path_).absolutePath();
    QString file_path = QFileDialog::getSaveFileName(
        this, QStringLiteral("Save stick_man project As"), initial_dir,
        QStringLiteral("stick_man Project (*.stickman)"));
    if (file_path.isEmpty()) {
        return false;
    }
    if (!file_path.endsWith(QStringLiteral(".stickman"), Qt::CaseInsensitive)) {
        file_path += QStringLiteral(".stickman");
    }
    if (!write_project_file(file_path)) {
        return false;
    }
    set_current_file(file_path);
    project_.mark_saved();
    return true;
}

void ui::stick_man::exit() {
    close();
}

void ui::stick_man::debug() {
}

void ui::stick_man::create_animation() {
    anim_pane_->create_animation();
}
void ui::stick_man::create_pose()
{
    anim_pane_->create_pose();
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
    QAction* actionNew = new QAction(tr("New"), this);
    QAction* actionOpen = new QAction(tr("Open..."), this);
    QAction* actionSave = new QAction(tr("Save"), this);
    QAction* actionSaveAs = new QAction(tr("Save As..."), this);
    QAction* actionExit = new QAction(tr("Exit"), this);
    actionNew->setShortcut(QKeySequence::New);
    actionOpen->setShortcut(QKeySequence::Open);
    actionSave->setShortcut(QKeySequence::Save);
    actionSaveAs->setShortcut(QKeySequence::SaveAs);
    actionExit->setShortcut(QKeySequence::Quit);
    file_menu->addAction(actionNew);
    file_menu->addAction(actionOpen);
    file_menu->addSeparator();
    file_menu->addAction(actionSave);
    file_menu->addAction(actionSaveAs);
    file_menu->addSeparator();
    file_menu->addAction(actionExit);
    QFontMetrics metrics(file_menu->font());
    int maxWidth = metrics.horizontalAdvance(actionSaveAs->text()) + 20;
    file_menu->setMinimumWidth(maxWidth);
    connect(actionNew, &QAction::triggered, this, &stick_man::new_file);
    connect(actionOpen, &QAction::triggered, this, &stick_man::open);
    connect(actionSave, &QAction::triggered, this, [this] { save(); });
    connect(actionSaveAs, &QAction::triggered, this, [this] { save_as(); });
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
    auto* view_menu = menuBar()->addMenu(tr("View"));

    auto* reset_view_action = view_menu->addAction(tr("Reset view"));
    connect(reset_view_action, &QAction::triggered, this, &stick_man::reset_view);
    view_menu->addSeparator();

    auto* panes_menu = view_menu->addMenu(tr("Panes"));
    panes_menu->addAction(tool_pal_->toggleViewAction());
    auto* tool_settings_action = tool_pane_->toggleViewAction();
    tool_settings_action->setText(tr("Tool settings"));
    panes_menu->addAction(tool_settings_action);
    panes_menu->addAction(skel_pane_->toggleViewAction());
    if (auto* artwork = findChild<pane::artwork_browser*>()) {
        auto* action = artwork->toggleViewAction();
        action->setText(tr("Artwork"));
        panes_menu->addAction(action);
    }
    panes_menu->addAction(anim_pane_->toggleViewAction());

    auto* canvas_view_menu = view_menu->addMenu(tr("Canvas view"));
    show_constraints_action_ = canvas_view_menu->addAction(tr("Show constraints"));
    show_constraints_action_->setObjectName("show_constraints");
    show_constraints_action_->setCheckable(true);
    connect(show_constraints_action_, &QAction::toggled, this, [this](bool show) {
        for (auto* canv : canvases_->canvases()) canv->set_constraints_view_visible(show);
    });
    show_constraints_action_->setChecked(true);

    auto* show_artwork = canvas_view_menu->addAction(tr("Show artwork"));
    show_artwork->setObjectName("show_artwork");
    show_artwork->setCheckable(true);
    show_artwork->setChecked(true);
    connect(show_artwork, &QAction::toggled, this, [this](bool show) {
        canvases_->active_canvas().artwork().set_show_artwork(show);
    });

    auto* skeleton_menu = canvas_view_menu->addMenu(tr("Skeleton"));
    auto* skeleton_group = new QActionGroup(this);
    skeleton_group->setExclusive(true);

    const auto add_skeleton_display_action = [this, skeleton_menu, skeleton_group](
        const QString& text, canvas::skeleton_display display, bool checked = false) {
        auto* action = skeleton_menu->addAction(text);
        action->setCheckable(true);
        action->setChecked(checked);
        skeleton_group->addAction(action);
        connect(action, &QAction::triggered, this, [this, display] {
            canvases_->active_canvas().artwork().set_skeleton_display(display);
        });
        return action;
    };

    add_skeleton_display_action(tr("Hidden"), canvas::skeleton_display::hidden);
    add_skeleton_display_action(tr("Wireframe (nodes only)"), canvas::skeleton_display::wireframe_nodes);
    add_skeleton_display_action(tr("Wireframe"), canvas::skeleton_display::wireframe);
    skeleton_visible_action_ = add_skeleton_display_action(
        tr("Visible"), canvas::skeleton_display::visible, true);

    auto* magnification_menu = view_menu->addMenu(tr("Canvas magnification"));
    auto* magnification_group = new QActionGroup(this);
    magnification_group->setExclusive(true);
    const auto* zoom_tool = static_cast<const tool::zoom*>(
        &tool_mgr_.tool_from_id(tool::id::zoom)
    );
    for (auto level : zoom_tool->magnification_levels()) {
        const auto level_str = std::to_string(level) + "%";
        auto* action = new QAction(level_str.c_str(), this);
        action->setCheckable(true);
        action->setChecked(level == 100);
        magnification_group->addAction(action);
        magnification_menu->addAction(action);
        connect(action, &QAction::triggered, this, [=]() {
            zoom_tool->do_zoom(level / 100.0);
        });
    }
}
void ui::stick_man::reset_view() {
    auto* artwork = findChild<pane::artwork_browser*>();

    // Rebuild the main pane layout rather than merely showing the panes. This also
    // brings floated/moved panes back to the same arrangement used on a fresh run.
    removeToolBar(tool_pal_);
    addToolBar(Qt::LeftToolBarArea, tool_pal_);
    tool_pal_->show();

    const auto remove_dock = [this](QDockWidget* dock) {
        dock->setFloating(false);
        removeDockWidget(dock);
    };
    remove_dock(tool_pane_);
    remove_dock(skel_pane_);
    remove_dock(anim_pane_);
    if (artwork) remove_dock(artwork);

    addDockWidget(Qt::RightDockWidgetArea, tool_pane_);
    addDockWidget(Qt::RightDockWidgetArea, skel_pane_);
    addDockWidget(Qt::RightDockWidgetArea, anim_pane_);
    if (artwork) {
        addDockWidget(Qt::RightDockWidgetArea, artwork);
        tabifyDockWidget(skel_pane_, artwork);
    }
    tabifyDockWidget(skel_pane_, anim_pane_);

    tool_pane_->show();
    skel_pane_->show();
    anim_pane_->show();
    if (artwork) artwork->show();
    skel_pane_->raise();

    if (show_constraints_action_) show_constraints_action_->setChecked(true);
    for (auto* canv : canvases_->canvases()) canv->set_constraints_view_visible(true);

    if (skeleton_visible_action_) skeleton_visible_action_->setChecked(true);
    canvases_->active_canvas().artwork().set_skeleton_display(canvas::skeleton_display::visible);
}

void ui::stick_man::insert_project_menu() {
    auto project_menu = menuBar()->addMenu(tr("Stick Man"));
    auto* new_animation = new QAction("Create new animation", this);
    auto* new_pose = new QAction("Create new pose", this);
    project_menu->addAction(new_animation);
    project_menu->addAction(new_pose);
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
void ui::stick_man::closeEvent(QCloseEvent* event) {
    if (maybe_save_changes() == save_decision::cancel) {
        event->ignore();
        return;
    }
    QMainWindow::closeEvent(event);
}

ui::stick_man::~stick_man() { anim_pane_->leave_animation(); }
