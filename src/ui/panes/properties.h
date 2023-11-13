#pragma once

#include <QtWidgets>
#include <unordered_map>
#include <functional>
#include <ranges>
#include "../util.h"
#include "../../model/project.h"
#include "props_box.h"

/*------------------------------------------------------------------------------------------------*/

namespace ui {

	class stick_man;

    namespace canvas {
        class canvas_manager;
        class scene;
    }

	enum class selection_type {
		none = 0,
		node,
		bone,
		skeleton,
		mixed
	};

    namespace pane {

        class skeleton;

        class selection_properties : public QStackedWidget {
            std::unordered_map<selection_type, props::props_box*> props_;
            pane::skeleton* skel_pane_;

            void handle_selection_changed(canvas::scene& canv);

        public:
            selection_properties(const props::current_canvas_fn& fn, pane::skeleton* pane);
            props::props_box* current_props() const;
            void set(const canvas::scene& canv);
            void init(canvas::canvas_manager& canvases, mdl::project& proj);
            pane::skeleton& skel_pane();
        };

    }
}