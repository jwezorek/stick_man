#include "properties.h"
#include "skeleton_properties.h"
#include "node_properties.h"
#include "bone_properties.h"
#include "../canvas/canvas.h"
#include "../canvas/canvas_item.h"
#include "../canvas/canvas_manager.h"
#include "../util.h"
#include "../stick_man.h"
#include "skeleton_pane.h"
#include "../../model/project.h"
#include "../../model/handle.h"
#include <unordered_map>
#include <unordered_set>
#include <numbers>
#include <qdebug.h>

/*------------------------------------------------------------------------------------------------*/

using namespace std::placeholders;
namespace r = std::ranges;
namespace rv = std::ranges::views;

namespace {

	ui::selection_type type_of_selection(const ui::selection_set& sel) {

		if (sel.empty()) {
			return ui::selection_type::none;
		}

		if (sel.size() == 1 && dynamic_cast<ui::skeleton_item*>(*sel.begin())) {
			return ui::selection_type::skeleton;
		}

		bool has_node = false;
		bool has_bone = false;
		for (auto itm_ptr : sel) {
			has_node = dynamic_cast<ui::node_item*>(itm_ptr) || has_node;
			has_bone = dynamic_cast<ui::bone_item*>(itm_ptr) || has_bone;
			if (has_node && has_bone) {
				return ui::selection_type::mixed;
			}
		}
		return has_node ? ui::selection_type::node : ui::selection_type::bone;
	}

	auto to_model_objects(r::input_range auto&& itms) {
		return itms |
			rv::transform(
				[](auto itm)->auto& {
					return itm->model();
				}
		);
	}
}

/*------------------------------------------------------------------------------------------------*/

ui::selection_properties::selection_properties(const props::current_canvas_fn& fn,
            skeleton_pane* sp) :
        skel_pane_(sp),
		props_{
			{selection_type::none, new props::no_properties(fn, this)},
			{selection_type::node, new props::node_properties(fn, this)},
			{selection_type::bone, new props::bone_properties(fn, this)},
			{selection_type::skeleton, new props::skeleton_properties(fn, this)},
			{selection_type::mixed, new props::mixed_properties(fn, this)}
		} {
	for (const auto& [key, prop_box] : props_) {
        QScrollArea* scroller = new QScrollArea();
        scroller->setWidget(prop_box);
        scroller->setWidgetResizable(true);
		addWidget(scroller);
	}
}

props::props_box* ui::selection_properties::current_props() const {
	return static_cast<props::props_box*>(
        static_cast<QScrollArea*>(currentWidget())->widget()
    );
}

void ui::selection_properties::set(const ui::canvas& canv) {
	auto* old_props = current_props();

    QScrollArea* scroller = nullptr;
    QWidget* widg = props_.at(type_of_selection(canv.selection()));
    while (scroller == nullptr) {
        scroller = dynamic_cast<QScrollArea*>(widg);
        widg = widg->parentWidget();
    }

	setCurrentWidget(
        scroller
    );

	old_props->lose_selection();
	current_props()->set_selection(canv);

	auto bone_items = ui::as_range_view_of_type<ui::bone_item>(canv.selection());
	for (ui::bone_item* bi : bone_items) {
		auto* tvi = bi->treeview_item();
	}
}

void ui::selection_properties::handle_selection_changed(canvas& canv) {
    set(canv);
}

void ui::selection_properties::init(canvas_manager& canvases, mdl::project& proj)
{
    for (const auto& [key, prop_box] : props_) {
        prop_box->init(proj);
    }
    set(canvases.active_canvas());
    connect(&canvases, &canvas_manager::selection_changed,
        this,
        &selection_properties::handle_selection_changed
    );
}

ui::skeleton_pane& ui::selection_properties::skel_pane() {
    return *skel_pane_;
}
