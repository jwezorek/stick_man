#include "animation_editor.hpp"
#include "../canvas/canvas_manager.hpp"
#include "../widgets/pose_strip.hpp"
#include "../widgets/timeline.hpp"
#include "../../model/project.hpp"
#include <QtWidgets>

ui::pane::animation_editor::animation_editor(QWidget* parent) :
    QDockWidget(tr("Animation Editor"), parent) {
    setObjectName("animation_editor");

    auto* content = new QWidget(this);
    auto* outer = new QVBoxLayout(content);
    outer->setContentsMargins(6, 6, 6, 6);

    auto* transport = new QHBoxLayout;
    auto make_transport = [&](const QString& text, const char* name) {
        auto* button = new QToolButton(content);
        button->setText(text);
        button->setObjectName(name);
        button->setEnabled(false);
        transport->addWidget(button);
        return button;
    };
    make_transport(QStringLiteral("|<"), "animation_transport_start");
    make_transport(QStringLiteral("Play"), "animation_transport_play");
    make_transport(QStringLiteral(">|"), "animation_transport_end");

    time_display_ = new QLabel(QStringLiteral("0:00.000"), content);
    time_display_->setObjectName("animation_time_display");
    transport->addSpacing(8);
    transport->addWidget(time_display_);
    transport->addStretch();
    outer->addLayout(transport);

    auto* body = new QHBoxLayout;
    auto* controls = new QVBoxLayout;

    auto* edit_domain = new QPushButton(tr("Edit Pose Domain"), content);
    edit_domain->setObjectName("edit_pose_domain");
    edit_domain->setEnabled(false);
    controls->addWidget(edit_domain);

    add_pose_ = new QPushButton(tr("Add Pose"), content);
    add_pose_->setObjectName("new_pose");
    controls->addWidget(add_pose_);

    duplicate_ = new QPushButton(tr("Duplicate"), content);
    duplicate_->setObjectName("duplicate_pose");
    controls->addWidget(duplicate_);

    rename_ = new QPushButton(tr("Rename"), content);
    rename_->setObjectName("rename_pose");
    controls->addWidget(rename_);

    delete_ = new QPushButton(tr("Delete"), content);
    delete_->setObjectName("delete_pose");
    controls->addWidget(delete_);

    previous_pose_ = new QCheckBox(tr("Show previous pose"), content);
    previous_pose_->setObjectName("show_previous_pose");
    controls->addWidget(previous_pose_);

    controls->addStretch();
    body->addLayout(controls);

    tabs_ = new QTabWidget(content);
    tabs_->setObjectName("animation_tabs");

    auto* pose_scroll = new QScrollArea(tabs_);
    pose_scroll->setObjectName("pose_strip_scroll");
    pose_scroll->setWidgetResizable(true);
    pose_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    pose_scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    pose_strip_ = new pose_strip;
    pose_scroll->setWidget(pose_strip_);
    tabs_->addTab(pose_scroll, tr("Poses"));

    artwork_timeline_ = new timeline(tabs_);
    artwork_timeline_->setObjectName("artwork_timeline");
    artwork_timeline_->set_rows(0);
    artwork_timeline_->set_items({});
    tabs_->addTab(artwork_timeline_, tr("Artwork"));

    body->addWidget(tabs_, 1);
    outer->addLayout(body, 1);
    setWidget(content);
    hide();

    connect(add_pose_, &QPushButton::clicked, this, [this] {
        if (project_) project_->add_animation_keyframe();
    });
    connect(duplicate_, &QPushButton::clicked, this, [this] {
        if (project_) project_->duplicate_animation_keyframe();
    });
    connect(rename_, &QPushButton::clicked, this, &animation_editor::rename_selected);
    connect(delete_, &QPushButton::clicked, this, [this] {
        if (project_) project_->delete_animation_keyframe();
    });
    connect(previous_pose_, &QCheckBox::toggled, this, [this](bool show) {
        if (project_) project_->set_show_previous_pose(show);
    });
    connect(pose_strip_, &pose_strip::keyframe_selected, this, [this](sm::object_id id) {
        if (project_) project_->select_animation_keyframe(id);
    });
}

void ui::pane::animation_editor::begin(mdl::project& project, canvas::manager& canvases,
        sm::object_id character, sm::object_id animation) {
    project_ = &project;
    canvases_ = &canvases;
    character_ = character;
    animation_ = animation;
    pose_strip_->set_context(project_, canvases_, character_, animation_);
    previous_pose_->setChecked(project.show_previous_pose());

    connect(&project, &mdl::project::animation_keyframe_selected, this,
        [this](sm::object_id) { refresh(); }, Qt::UniqueConnection);
    connect(&project, &mdl::project::animation_preview_changed, this,
        &animation_editor::refresh, Qt::UniqueConnection);
    connect(&project, &mdl::project::project_changed, this,
        [this](mdl::project&) { refresh(); }, Qt::UniqueConnection);

    refresh();
    show();
    raise();
}

void ui::pane::animation_editor::end() {
    if (project_) {
        disconnect(project_, nullptr, this, nullptr);
    }
    project_ = nullptr;
    canvases_ = nullptr;
    character_ = {};
    animation_ = {};
    pose_strip_->set_context(nullptr, nullptr, {}, {});
    hide();
}

void ui::pane::animation_editor::refresh() {
    pose_strip_->refresh();
    const bool has_selection = project_ && project_->animation_session_keyframe().has_value();
    duplicate_->setEnabled(has_selection);
    rename_->setEnabled(has_selection);
    delete_->setEnabled(has_selection);
}

void ui::pane::animation_editor::rename_selected() {
    if (!project_) return;

    const auto selected = project_->animation_session_keyframe();
    if (!selected) return;

    auto character = project_->core().character(character_);
    auto* animation = character ?
        character->get().animation_data().find_animation(animation_) : nullptr;
    auto* keyframe = animation ? animation->find_keyframe(*selected) : nullptr;
    if (!keyframe) return;

    bool ok = false;
    const QString current = keyframe->name ?
        QString::fromStdString(*keyframe->name) : QString{};
    const QString value = QInputDialog::getText(this, tr("Rename pose"),
        tr("Name (leave empty for automatic Pose N label):"),
        QLineEdit::Normal, current, &ok);
    if (!ok) return;

    const auto trimmed = value.trimmed();
    if (trimmed.isEmpty()) {
        project_->rename_animation_keyframe(std::nullopt);
    } else {
        project_->rename_animation_keyframe(
            std::optional<std::string>{trimmed.toStdString()});
    }
}
