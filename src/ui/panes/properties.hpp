#pragma once

#include <QtWidgets>
#include <unordered_map>
#include <functional>
#include <ranges>
#include "../util.hpp"
#include "../../model/project.hpp"
#include "props_box.hpp"

/*------------------------------------------------------------------------------------------------*/

namespace ui {

	class stick_man;

    namespace canvas {
        class manager;
        class scene;
    }

	enum class selection_type {
		none = 0,
		node,
		bone,
		skeleton,
        character,
		constraint,
		mixed
	};

    namespace pane {

        class skeleton;

        class selection_properties : public QStackedWidget {
            std::unordered_map<selection_type, props::props_box*> props_;
            pane::skeleton* skel_pane_;
            bool read_only_ = false;

            void handle_selection_changed(canvas::scene& canv);
            void apply_read_only(props::props_box& props);

        public:
            selection_properties(const props::current_canvas_fn& fn, pane::skeleton* pane);
            props::props_box* current_props() const;
            void set(const canvas::scene& canv);
            void init(canvas::manager& canvases, mdl::project& proj);
            void set_read_only(bool read_only);
            bool read_only() const { return read_only_; }
            pane::skeleton& skel_pane();
        };

    }
}
