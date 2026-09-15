#pragma once

#include <QtWidgets/QDockWidget>

/*------------------------------------------------------------------------------------------------*/

namespace mdl {
    class project;
}

namespace ui {

    namespace canvas {
        class manager;
    }

    namespace pane {

        class animation_skeleton_pane;

        class animation : public QDockWidget {
            animation_skeleton_pane* content_;

        public:
            animation(QWidget* wnd);
            void init(canvas::manager& canvases, mdl::project& proj);
        };

    }

}
