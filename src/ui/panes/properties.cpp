#include "properties.h"
#include "skeleton_properties.h"
#include "node_properties.h"
#include "bone_properties.h"
#include "../canvas/scene.h"
#include "../canvas/skel_item.h"
#include "../canvas/node_item.h"
#include "../canvas/bone_item.h"
#include "../canvas/canvas_manager.h"
#include "../util.h"
#include "../stick_man.h"
#include "main_skeleton.h"
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

	ui::selection_type type_of_selection(const ui::canvas::selection_set& sel) {

		if (sel.empty()) {
			return ui::selection_type::none;
		}

		if (sel.size() == 1 && dynamic_cast<ui::canvas::item::skeleton*>(*sel.begin())) {
			return ui::selection_type::skeleton;
		}

		bool has_node = false;
		bool has_bone = false;
		for (auto itm_ptr : sel) {
			has_node = dynamic_cast<ui::canvas::item::node*>(itm_ptr) || has_node;
			has_bone = dynamic_cast<ui::canvas::item::bone*>(itm_ptr) || has_bone;
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

ui::pane::selection_properties::selection_properties(const props::current_canvas_fn& fn,
            pane::skeleton* sp) :
        skel_pane_(sp),
		props_{
			{selection_type::none, new props::no_properties(fn, this)},
			{selection_type::node, new props::nodes(fn, this)},
			{selection_type::bone, new props::bones(fn, this)},
			{selection_type::skeleton, new props::skeletons(fn, this)},
			{selection_type::mixed, new props::mixed_properties(fn, this)}
		} {
	for (const auto& [key, prop_box] : props_) {
        QScrollArea* scroller = new QScrollArea();
        scroller->setWidget(prop_box);
        scroller->setWidgetResizable(true);
		addWidget(scroller);
	}
}

ui::pane::props::props_box* ui::pane::selection_properties::current_props() const {
	return static_cast<props::props_box*>(
        static_cast<QScrollArea*>(currentWidget())->widget()
    );
}

void ui::pane::selection_properties::set(const ui::canvas::scene& canv) {
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

	auto bone_items = ui::as_range_view_of_type<ui::canvas::item::bone>(canv.selection());
	for (ui::canvas::item::bone* bi : bone_items) {
		auto* tvi = bi->treeview_item();
	}
}

void ui::pane::selection_properties::handle_selection_changed(canvas::scene& canv) {
    set(canv);
}

void ui::pane::selection_properties::init(canvas::manager& canvases, mdl::project& proj)
{
    for (const auto& [key, prop_box] : props_) {
        prop_box->init(proj);
    }
    set(canvases.active_canvas());
    connect(&canvases, &canvas::manager::selection_changed,
        this,
        &selection_properties::handle_selection_changed
    );
}

ui::pane::skeleton& ui::pane::selection_properties::skel_pane() {
    return *skel_pane_;
}
