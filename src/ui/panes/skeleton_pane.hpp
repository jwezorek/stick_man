#pragma once

#include "../canvas/scene.hpp"
#include <QWidget>
#include <QtWidgets>
#include "../../core/sm_types.hpp"
#include <functional>

/*------------------------------------------------------------------------------------------------*/

namespace ui {

	class manager;
	class stick_man;
    namespace canvas {
        class manager;
    }

    namespace pane {

        class main_skeleton_pane;

        class skeleton : public QDockWidget {

            main_skeleton_pane* main_skel_pane_;

        public:

            skeleton(ui::stick_man* mgr);
            void init(canvas::manager& canvases, mdl::project& proj);
            void set_animation_mode(bool active);

        };
    }
}