#include "animation_editor.hpp"
#include "../widgets/pose_strip.hpp"
#include "../widgets/timeline.hpp"

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
    auto* pose_controls = new QVBoxLayout;
    auto* edit_domain = new QPushButton(tr("Edit Pose Domain"), content);
    edit_domain->setObjectName("edit_pose_domain");
    auto* new_pose = new QPushButton(tr("New Pose"), content);
    new_pose->setObjectName("new_pose");
    pose_controls->addWidget(edit_domain);
    pose_controls->addWidget(new_pose);
    pose_controls->addStretch();
    body->addLayout(pose_controls);

    tabs_ = new QTabWidget(content);
    tabs_->setObjectName("animation_tabs");
    pose_strip_ = new pose_strip(tabs_);
    tabs_->addTab(pose_strip_, tr("Poses"));

    artwork_timeline_ = new timeline(tabs_);
    artwork_timeline_->setObjectName("artwork_timeline");
    artwork_timeline_->set_rows(0);
    artwork_timeline_->set_items({});
    tabs_->addTab(artwork_timeline_, tr("Artwork"));
    body->addWidget(tabs_, 1);
    outer->addLayout(body, 1);

    setWidget(content);
    hide();
}

void ui::pane::animation_editor::begin() {
    show();
    raise();
}

void ui::pane::animation_editor::end() {
    hide();
}
