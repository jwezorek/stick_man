#pragma once

#include <QDockWidget>

namespace ui { class timeline; class pose_strip; }
class QLabel;
class QTabWidget;

namespace ui::pane {

class animation_editor : public QDockWidget {
    Q_OBJECT
public:
    explicit animation_editor(QWidget* parent = nullptr);
    void begin();
    void end();

private:
    QTabWidget* tabs_ = nullptr;
    pose_strip* pose_strip_ = nullptr;
    timeline* artwork_timeline_ = nullptr;
    QLabel* time_display_ = nullptr;
};

} // namespace ui::pane
