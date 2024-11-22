#include "skeleton_properties.h"
#include "../canvas/scene.h"
#include "../canvas/skel_item.h"
#include "skeleton_pane.h"
#include "main_skeleton_pane.h"

/*------------------------------------------------------------------------------------------------*/

ui::pane::props::skeletons::skeletons(
        const current_canvas_fn& fn,  selection_properties* parent) :
    props_box(fn, parent, "skeleton selection") {
}

void ui::pane::props::skeletons::populate(mdl::project & proj) {
    layout_->addWidget(
        name_ = new ui::labeled_field("   name", "")
    );
    name_->set_color(QColor("yellow"));
    name_->value()->set_validator(
        [this](const std::string& new_name)->bool {
            return parent_->skel_pane().validate_props_name_change(new_name);
        }
    );

    connect(&proj, &mdl::project::name_changed,
        [this](mdl::skel_piece piece, const std::string& new_name) {
            handle_rename(piece, name_->value(), new_name);
        }
    );

    connect(
        name_->value(), &ui::string_edit::value_changed,
        this, &skeletons::do_property_name_change
    );
}

void ui::pane::props::skeletons::set_selection(const ui::canvas::scene& canv) {
    auto* skel_item = canv.selected_skeleton();
    name_->set_value(skel_item->model().name().c_str());
}