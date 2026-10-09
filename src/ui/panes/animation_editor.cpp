#include "animation_editor.hpp"
#include "../animation_playback.hpp"
#include "../canvas/canvas_manager.hpp"
#include "../widgets/pose_strip.hpp"
#include "../widgets/timeline.hpp"
#include "../../model/project.hpp"
#include <QtWidgets>
#include <cmath>
#include <algorithm>
#include <variant>

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
    play_->setToolTip(tr("Play / pause skeletal preview"));

    time_display_ = new QLabel(QStringLiteral("0:00.000"), content);
    time_display_->setObjectName("animation_time_display");
    transport->addSpacing(8);
    transport->addWidget(time_display_);
    preview_status_ = new QLabel(content);
    preview_status_->setObjectName("animation_preview_status");
    transport->addWidget(preview_status_);
    transport->addStretch();
    outer->addLayout(transport);

    auto* body = new QHBoxLayout;
    auto* controls = new QVBoxLayout;

    add_pose_ = new QPushButton(tr("Add Pose"), content);
    add_pose_->setObjectName("new_pose");
    controls->addWidget(add_pose_);

    insert_pose_ = new QPushButton(tr("Insert Pose"), content);
    insert_pose_->setObjectName("insert_pose");
    insert_pose_->setEnabled(false);
    controls->addWidget(insert_pose_);

    previous_pose_ = new QCheckBox(tr("Show previous pose"), content);
    previous_pose_->setObjectName("show_previous_pose");
    controls->addWidget(previous_pose_);

    controls->addSpacing(8);
    controls->addWidget(new QLabel(tr("Transition duration (s)"), content));
    transition_duration_ = new QDoubleSpinBox(content);
    transition_duration_->setObjectName("transition_duration");
    transition_duration_->setDecimals(6);
    transition_duration_->setRange(0.000001, 1.0e9);
    transition_duration_->setEnabled(false);
    controls->addWidget(transition_duration_);

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
        if (playback_->playing()) {
            // Pausing leaves us in preview; do not restore the pre-playback selection.
            playback_return_keyframe_.reset();
            playback_->pause();
        } else {
            // Remember the editing frame only for uninterrupted playback.
            // Sampling deliberately clears the visible selection while playing.
            playback_return_keyframe_ = project_ && playback_->duration() > 0
                ? project_->animation_session_keyframe() : std::nullopt;
            preview_requested_ = true;
            if (playback_->duration() > 0) playback_->play();
            else preview_time(0);
        }
    });
    connect(start_, &QToolButton::clicked, this, [this] { preview_time(0.0); });
    connect(end_, &QToolButton::clicked, this, [this] { preview_time(playback_->duration()); });
    connect(playback_, &animation_playback::playing_changed, this, [this](bool playing) {
        play_->setText(playing ? tr("Pause") : tr("Play"));
        pose_strip_->set_playback_active(playing);
    });
    connect(playback_, &animation_playback::finished, this, [this] {
        // An ordinary play-through started from a selected editing frame returns
        // to that frame. Playback started from preview (no selection) stays in preview.
        const auto keyframe = playback_return_keyframe_;
        playback_return_keyframe_.reset();
        if (project_ && preview_requested_ && keyframe)
            project_->select_animation_keyframe(*keyframe);
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
        if (preview_requested_ && project_) project_->preview_animation_time(seconds);
    });
    connect(pose_strip_, &pose_strip::playback_focus_changed, this, [pose_scroll](QRectF region) {
        if (pose_scroll->horizontalScrollBar()->isSliderDown())
            return;
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
    connect(insert_pose_, &QPushButton::clicked, this, [this] {
        if (!project_)
            return;
        if (project_->insert_animation_keyframe(playback_->time()) == sm::result::success) {
            selected_transition_.reset();
            pose_strip_->set_selected_transition({});
        }
    });
    connect(previous_pose_, &QCheckBox::toggled, this, [this](bool show) {
        if (project_) project_->set_show_previous_pose(show);
    });
    connect(pose_strip_, &pose_strip::keyframe_selected, this, [this](sm::object_id id) {
        selected_transition_.reset();
        pose_strip_->set_selected_transition({});
        transition_duration_->setEnabled(false);
        if (project_) project_->select_animation_keyframe(id);
    });
    connect(pose_strip_, &pose_strip::scrub_requested, this, [this](double seconds) {
        preview_time(seconds);
    });
    connect(pose_strip_, &pose_strip::transition_selected, this, [this](sm::object_id id) {
        selected_transition_ = id;
        pose_strip_->set_selected_transition(id);
        playback_->hold();
        refresh();
    });
    connect(pose_strip_, &pose_strip::transition_duration_requested,
        this, [this](sm::object_id id, double seconds) {
            if (!project_)
                return;
            playback_->hold();
            selected_transition_ = id;
            pose_strip_->set_selected_transition(id);
            if (project_->set_animation_transition_duration(id, seconds) == sm::result::success)
                preview_requested_ = false;
        });
    connect(transition_duration_, &QDoubleSpinBox::editingFinished, this, [this] {
        if (!project_ || !selected_transition_)
            return;
        if (project_->set_animation_transition_duration(*selected_transition_, transition_duration_->value())
                == sm::result::success) {
            preview_requested_ = false;
        }
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
    connect(&project, &mdl::project::animation_editing_requested, this, [this] {
        preview_requested_ = false;
        playback_return_keyframe_.reset();
        playback_->hold();
    });
    connect(&project, &mdl::project::animation_display_status_changed,
        this, &animation_editor::update_preview_status);
    connect(&project, &mdl::project::animation_display_status_changed,
        this, &animation_editor::refresh);
    connect(&project, &mdl::project::animation_authoring_error, this, [this](const QString& message) {
        preview_status_->setText(message);
    });
    connect(&project, &mdl::project::animation_session_ending, this, &animation_editor::end);
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
    preview_requested_ = false;
    playback_return_keyframe_.reset();
    selected_transition_.reset();
    pose_strip_->set_selected_transition({});
    if (project_) project_->exit_animation_preview();
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

ui::pane::animation_editor::~animation_editor() { end(); }

void ui::pane::animation_editor::preview_time(double seconds) {
    if (!project_ || !std::isfinite(seconds))
        return;
    // Seeking/scrubbing breaks the play-through return-to-editing behavior.
    playback_return_keyframe_.reset();
    preview_requested_ = true;
    playback_->seek(seconds);
}

void ui::pane::animation_editor::update_preview_status() {
    if (!project_) {
        preview_status_->clear();
        return;
    }
    switch (project_->preview_status()) {
    case mdl::animation_display_status::sampled:
        preview_status_->setText(tr("Playback preview (read-only)"));
        break;
    case mdl::animation_display_status::sampling_failed:
        if (project_->preview_error() == sm::result::invalid_animation)
            preview_status_->setText(tr("Preview failed: animation data is inconsistent (for example, mismatched pinned endpoints). Showing editing pose."));
        else
            preview_status_->setText(tr("Preview failed: cannot solve pose. Showing editing pose."));
        break;
    case mdl::animation_display_status::reconstruction_failed:
        preview_status_->setText(tr("Preview failed: cannot display pose. Showing editing pose."));
        break;
    case mdl::animation_display_status::empty:
        preview_status_->setText(tr("No poses to preview."));
        break;
    default:
        preview_status_->setText(tr("Editing pose"));
        break;
    }
}

void ui::pane::animation_editor::refresh() {
    const bool timing_changed = pose_strip_->refresh();
    const sm::animation* animation = nullptr;
    if (project_) {
        auto owner = project_->core().character(character_);
        if (owner)
            animation = owner->get().animation_data().find_animation(animation_);
    }
    if (timing_changed) {
        if (project_) project_->exit_animation_preview();
        playback_->set_duration(animation ? animation->duration_seconds() : 0);
    }
    const bool has_poses = animation && !animation->keyframes.empty();
    play_->setEnabled(has_poses);
    start_->setEnabled(has_poses);
    end_->setEnabled(has_poses);

    bool interior = false;
    if (animation) {
        try {
            auto sample = sm::sample_reference_pose(*animation, playback_->time());
            interior = sample && std::holds_alternative<sm::reference_transition>(sample->location);
        } catch (...) { interior = false; }
    }
    insert_pose_->setEnabled(interior && project_ && project_->preview_status() == mdl::animation_display_status::sampled);

    const sm::pose_transition* selected = nullptr;
    if (animation && selected_transition_) {
        auto it = std::find_if(animation->transitions.begin(), animation->transitions.end(),
            [this](const auto& t) { return t.id == *selected_transition_; });
        if (it != animation->transitions.end())
            selected = &*it;
        else {
            selected_transition_.reset();
            pose_strip_->set_selected_transition({});
        }
    }
    transition_duration_->setEnabled(selected != nullptr);
    if (selected) {
        QSignalBlocker blocker(transition_duration_);
        transition_duration_->setValue(selected->duration_seconds);
    }
}

