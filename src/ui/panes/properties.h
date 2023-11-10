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
    class skeleton_pane;
    class project;

	enum class selection_type {
		none = 0,
		node,
		bone,
		skeleton,
		mixed
	};

	class selection_properties : public QStackedWidget {
		std::unordered_map<selection_type, props::props_box*> props_;
        skeleton_pane* skel_pane_;

        void handle_selection_changed(canvas& canv);

	public:
		selection_properties(const props::current_canvas_fn& fn, skeleton_pane* pane);
		props::props_box* current_props() const;
		void set(const ui::canvas& canv);
		void init(canvas_manager& canvases, mdl::project& proj);
        skeleton_pane& skel_pane();
	};

    constexpr double k_tolerance = 0.00005;
    std::optional<double> get_unique_val(auto vals, double tolerance = k_tolerance) {
        using namespace std::placeholders;
        namespace r = std::ranges;
        namespace rv = std::ranges::views;
        if (vals.empty()) {
            return {};
        }
        auto first_val = *vals.begin();
        auto first_not_equal = r::find_if(
            vals,
            std::not_fn(std::bind(ui::is_approximately_equal, _1, first_val, tolerance))
        );
        return  (first_not_equal == vals.end()) ?
            std::optional<double>{first_val} : std::optional<double>{};
    }
}