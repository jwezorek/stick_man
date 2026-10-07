#include "properties.hpp"
#include "skeleton_properties.hpp"
#include "node_properties.hpp"
#include "bone_properties.hpp"
#include "constraint_properties.hpp"
#include "../canvas/scene.hpp"
#include "../canvas/skel_item.hpp"
#include "../canvas/node_item.hpp"
#include "../canvas/bone_item.hpp"
#include "../canvas/canvas_manager.hpp"
#include "../util.hpp"
#include "../stick_man.hpp"
#include "main_skeleton_pane.hpp"
#include "../../model/project.hpp"
#include "../../model/handle.hpp"
#include <unordered_map>
#include <unordered_set>
#include <numbers>
#include <qdebug.h>

/*------------------------------------------------------------------------------------------------*/

namespace r = std::ranges;
namespace rv = std::ranges::views;

namespace {

	ui::selection_type type_of_selection(const ui::canvas::scene& canv) {

        if (canv.selected_constraint_id())
            return ui::selection_type::constraint;
        const auto& sel = canv.selection();
		if (sel.empty()) {
			return ui::selection_type::none;
		}
        if (sel.size() == 1 && dynamic_cast<ui::canvas::item::character*>(*sel.begin()))
            return ui::selection_type::character;

		if (r::all_of(sel, [](auto* item) { return dynamic_cast<ui::canvas::item::skeleton*>(item) != nullptr; })) {
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
            {selection_type::character, new props::character(fn, this)},
            {selection_type::constraint, new props::constraint_properties(fn, this)},
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
    QWidget* widg = props_.at(type_of_selection(canv));
    while (scroller == nullptr) {
        scroller = dynamic_cast<QScrollArea*>(widg);
        widg = widg->parentWidget();
    }

	setCurrentWidget(
        scroller
    );

	old_props->lose_selection();
	current_props()->set_selection(canv);
    apply_read_only(*current_props());

	auto bone_items = ui::as_range_view_of_type<ui::canvas::item::bone>(canv.selection());
	for (ui::canvas::item::bone* bi : bone_items) {
		auto* tvi = bi->treeview_item();
	}
}

void ui::pane::selection_properties::handle_selection_changed(canvas::scene& canv) {
    set(canv);
}

void ui::pane::selection_properties::apply_read_only(props::props_box& props) {
    for (auto* edit : props.findChildren<QLineEdit*>())
        edit->setReadOnly(read_only_);

    for (auto* combo : props.findChildren<QComboBox*>())
        combo->setEnabled(!read_only_);

    // Navigation links remain live in read-only mode; ordinary buttons mutate
    // the project and therefore do not.
    for (auto* button : props.findChildren<QPushButton*>())
        if (!dynamic_cast<ui::hyperlink_button*>(button))
            button->setEnabled(!read_only_);
}

void ui::pane::selection_properties::set_read_only(bool read_only) {
    read_only_ = read_only;
    for (const auto& [_, prop_box] : props_)
        apply_read_only(*prop_box);
}

void ui::pane::selection_properties::init(canvas::manager& canvases, mdl::project& proj)
{
    for (const auto& [key, prop_box] : props_) {
        prop_box->init(proj);
    }
    set(canvases.active_canvas());
    // Properties describe the objects actually selected in the canvas.  In
    // Animation Mode those objects belong to the detached working topology, so
    // use the view-selection signal rather than the persistent-project-only one.
    connect(&canvases, &canvas::manager::view_selection_changed,
        this,
        &selection_properties::handle_selection_changed
    );
}

ui::pane::skeleton& ui::pane::selection_properties::skel_pane() {
    return *skel_pane_;
}
