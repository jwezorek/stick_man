#include "animation_editor.hpp"
#include "../animation_playback.hpp"
#include "../canvas/canvas_manager.hpp"
#include "../widgets/pose_strip.hpp"
#include "../widgets/timeline.hpp"
#include "../../model/project.hpp"
#include <QtWidgets>
#include <cmath>

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
    start_ = make_transport(QStringLiteral("|<"), "animation_transport_start");
    play_ = make_transport(tr("Play"), "animation_transport_play");
    end_ = make_transport(QStringLiteral(">|"), "animation_transport_end");
    start_->setToolTip(tr("Stop and return to first pose"));
    end_->setToolTip(tr("Go to final pose"));
    play_->setToolTip(tr("Play / pause Pose Strip timing"));

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

    playback_ = new animation_playback(this);
    connect(play_, &QToolButton::clicked, this, [this] {
        if (playback_->playing()) playback_->pause();
        else playback_->play();
    });
    connect(start_, &QToolButton::clicked, playback_, &animation_playback::stop);
    connect(end_, &QToolButton::clicked, playback_, &animation_playback::go_to_end);
    connect(playback_, &animation_playback::playing_changed, this, [this](bool playing) {
        play_->setText(playing ? tr("Pause") : tr("Play"));
    });
    connect(playback_, &animation_playback::time_changed,
        pose_strip_, &pose_strip::set_playback_time);
    connect(playback_, &animation_playback::time_changed, this, [this](double seconds) {
        const auto ms = qRound64(std::fmod(seconds, 60.0) * 1000);
        const double minutes = std::floor(seconds / 60.0) + ms / 60000;
        time_display_->setText(QStringLiteral("%1:%2.%3")
            .arg(QString::number(minutes, 'f', 0))
            .arg((ms / 1000) % 60, 2, 10, QLatin1Char('0'))
            .arg(ms % 1000, 3, 10, QLatin1Char('0')));
    });
    connect(pose_strip_, &pose_strip::playback_focus_changed, this,
        [pose_scroll](QRectF region) {
            if (pose_scroll->horizontalScrollBar()->isSliderDown()) return;
            // Reveal only the clipped edge; never continually center the playhead.
            pose_scroll->ensureVisible(qRound(region.left()), qRound(region.center().y()), 16, 0);
            pose_scroll->ensureVisible(qRound(region.right()), qRound(region.center().y()), 16, 0);
        });

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
    end();
    project_ = &project;
    canvases_ = &canvases;
    character_ = character;
    animation_ = animation;
    pose_strip_->set_context(project_, canvases_, character_, animation_);
    previous_pose_->setChecked(project.show_previous_pose());

    connect(&project, &mdl::project::animation_keyframe_selected, this,
        &animation_editor::refresh, Qt::UniqueConnection);
    connect(&project, &mdl::project::animation_preview_changed, this,
        &animation_editor::refresh, Qt::UniqueConnection);
    connect(&project, &mdl::project::project_changed, this,
        &animation_editor::refresh, Qt::UniqueConnection);
    connect(&project, &QObject::destroyed, this, [this] {
        project_ = nullptr;
        end();
    });

    refresh();
    auto owner = project.core().character(character_);
    const auto* source = owner ? owner->get().animation_data().find_animation(animation_) : nullptr;
    playback_->set_duration(source ? source->duration_seconds() : 0);
    show();
    raise();
}

void ui::pane::animation_editor::end() {
    playback_->set_duration(0);
    if (project_) {
        disconnect(project_, nullptr, this, nullptr);
    }
    project_ = nullptr;
    canvases_ = nullptr;
    character_ = {};
    animation_ = {};
    pose_strip_->set_context(nullptr, nullptr, {}, {});
    play_->setEnabled(false);
    start_->setEnabled(false);
    end_->setEnabled(false);
    hide();
}

void ui::pane::animation_editor::refresh() {
    const bool timing_changed = pose_strip_->refresh();
    const sm::animation* animation = nullptr;
    if (project_) {
        auto owner = project_->core().character(character_);
        if (owner) animation = owner->get().animation_data().find_animation(animation_);
    }
    if (timing_changed) playback_->set_duration(animation ? animation->duration_seconds() : 0);
    const bool has_poses = animation && !animation->keyframes.empty();
    play_->setEnabled(has_poses && animation->duration_seconds() > 0);
    start_->setEnabled(has_poses);
    end_->setEnabled(has_poses);
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
