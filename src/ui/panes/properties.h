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
    class canvas_manager;
	class canvas;
    class project;

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

            void handle_selection_changed(canvas& canv);

        public:
            selection_properties(const props::current_canvas_fn& fn, pane::skeleton* pane);
            props::props_box* current_props() const;
            void set(const ui::canvas& canv);
            void init(canvas_manager& canvases, mdl::project& proj);
            pane::skeleton& skel_pane();
        };

    }
}