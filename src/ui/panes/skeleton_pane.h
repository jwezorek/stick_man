#pragma once

#include "../canvas/scene.h"
#include <QWidget>
#include <QtWidgets>
#include "../../core/sm_types.h"
#include "properties.h"
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
        class animation_skeleton_pane;

        class skeleton : public QDockWidget {

            canvas::manager* canvases_;
            mdl::project* project_;
            main_skeleton_pane* main_skel_pane_;
            animation_skeleton_pane* anim_skel_pane_;

        public:

            skeleton(ui::stick_man* mgr);
            selection_properties& sel_properties();
            void init(canvas::manager& canvases, mdl::project& proj);
            bool validate_props_name_change(const std::string& new_name);

        };
    }
}