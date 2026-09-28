#pragma once
#include "../../core/sm_object_id.hpp"
#include <QDockWidget>

namespace mdl { class project; }
namespace ui {
    class timeline;
    class pose_strip;
    class animation_playback;
    namespace canvas { class manager; }
}
class QLabel;
class QTabWidget;
class QPushButton;
class QCheckBox;
class QToolButton;

namespace ui::pane {
class animation_editor : public QDockWidget {
    Q_OBJECT
public:
    explicit animation_editor(QWidget* parent = nullptr);
    void begin(mdl::project& project, canvas::manager& canvases,
        sm::object_id character, sm::object_id animation);
    void end();
    ~animation_editor() override;
    // Deterministic absolute-time evaluation, also used by transport updates.
    void preview_time(double seconds);
private:
    mdl::project* project_ = nullptr;
    canvas::manager* canvases_ = nullptr;
    sm::object_id character_;
    sm::object_id animation_;
    QTabWidget* tabs_ = nullptr;
    pose_strip* pose_strip_ = nullptr;
    timeline* artwork_timeline_ = nullptr;
    QLabel* time_display_ = nullptr;
    QLabel* preview_status_ = nullptr;
    bool preview_requested_ = false;
    QPushButton* add_pose_ = nullptr;
    QPushButton* duplicate_ = nullptr;
    QPushButton* rename_ = nullptr;
    QPushButton* delete_ = nullptr;
    QCheckBox* previous_pose_ = nullptr;
    animation_playback* playback_ = nullptr;
    QToolButton* play_ = nullptr;
    QToolButton* start_ = nullptr;
    QToolButton* end_ = nullptr;

    void refresh();
    void update_preview_status();
    void rename_selected();
};
}
