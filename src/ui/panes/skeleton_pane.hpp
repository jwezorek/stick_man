#pragma once

#include "../canvas/scene.hpp"
#include <QWidget>
#include <QtWidgets>
#include "../../core/sm_types.hpp"
#include "properties.hpp"
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
            selection_properties& sel_properties();
            void init(canvas::manager& canvases, mdl::project& proj);
            bool validate_props_name_change(const std::string& new_name);

        };
    }
}