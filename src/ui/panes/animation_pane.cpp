#include "animation_pane.hpp"
#include "animation_skeleton_pane.hpp"
#include <QIcon>

/*------------------------------------------------------------------------------------------------*/

ui::pane::animation::animation(QWidget* wnd) :
        QDockWidget(tr("Animation"), wnd),
        content_(new animation_skeleton_pane(this)) {
    setWindowIcon(QIcon(":/images/move_icon.png"));
    setWidget(content_);
}

void ui::pane::animation::init(canvas::manager& canvases, mdl::project& proj) {
    content_->init(canvases, proj);
}
