#pragma once
#include "../../core/sm_object_id.hpp"
#include <QDockWidget>

namespace mdl { class project; }
namespace ui {
    class timeline;
    class pose_strip;
    namespace canvas { class manager; }
}
class QLabel;
class QTabWidget;
class QPushButton;
class QCheckBox;

namespace ui::pane {
class animation_editor : public QDockWidget {
    Q_OBJECT
public:
    explicit animation_editor(QWidget* parent = nullptr);
    void begin(mdl::project& project, canvas::manager& canvases,
        sm::object_id character, sm::object_id animation);
    void end();
private:
    mdl::project* project_ = nullptr;
    canvas::manager* canvases_ = nullptr;
    sm::object_id character_;
    sm::object_id animation_;
    QTabWidget* tabs_ = nullptr;
    pose_strip* pose_strip_ = nullptr;
    timeline* artwork_timeline_ = nullptr;
    QLabel* time_display_ = nullptr;
    QPushButton* add_pose_ = nullptr;
    QPushButton* duplicate_ = nullptr;
    QPushButton* rename_ = nullptr;
    QPushButton* delete_ = nullptr;
    QCheckBox* previous_pose_ = nullptr;

    void refresh();
    void rename_selected();
};
}
