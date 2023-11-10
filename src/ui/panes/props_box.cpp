#include "props_box.h"
#include "properties.h"
#include "node_properties.h"
#include "bone_properties.h"
#include "../canvas/canvas.h"

props::props_box::props_box(
        const current_canvas_fn& fn,
        ui::selection_properties* parent, QString title) :
    QWidget(parent),
    get_current_canv_(fn),
    parent_(parent),
    title_(nullptr),
    layout_(new QVBoxLayout(this)) {
    layout_->addWidget(title_ = new QLabel(title));
    hide();
}

void props::props_box::do_property_name_change(const std::string& new_name) {
    auto maybe_piece = ui::selected_single_model(get_current_canv_());
    if (!maybe_piece) {
        return;
    }
    proj_->rename(*maybe_piece, new_name);
}

void props::props_box::handle_rename(mdl::skel_piece p,
    ui::string_edit* name_edit, const std::string& new_name)
{
    auto maybe_piece = ui::selected_single_model(get_current_canv_());
    if (!maybe_piece) {
        return;
    }
    if (!mdl::identical_pieces(*maybe_piece, p)) {
        return;
    }
    if (name_edit->text().toStdString() != new_name) {
        name_edit->setText(new_name.c_str());
    }
}

void props::props_box::set_title(QString title) {
    title_->setText(title);
}

void props::props_box::init(mdl::project& proj) {
    populate(proj);
    layout_->addStretch();
    proj_ = &proj;
}

void props::props_box::populate(mdl::project& proj) {

}

void props::props_box::lose_selection() {
}

/*------------------------------------------------------------------------------------------------*/

props::single_or_multi_props_widget::single_or_multi_props_widget(
        const props::current_canvas_fn& fn, ui::selection_properties* parent, QString title) :
    props::props_box(fn, parent, title)
{}

void props::single_or_multi_props_widget::set_selection(const ui::canvas& canv) {
    set_selection_common(canv);
    if (is_multi(canv)) {
        set_selection_multi(canv);
    }
    else {
        set_selection_single(canv);
    }
}

/*------------------------------------------------------------------------------------------------*/

props::no_properties::no_properties(const props::current_canvas_fn& fn,
        ui::selection_properties* parent) :
        props::props_box(fn, parent, "no selection") {
}

void props::no_properties::set_selection(const ui::canvas& canv) {
}

/*------------------------------------------------------------------------------------------------*/

props::mixed_properties::mixed_properties::mixed_properties(
        const props::current_canvas_fn& fn, ui::selection_properties* parent) :
    props_box(fn, parent, "") {
}

void props::mixed_properties::populate(mdl::project& proj) {
    layout_->addWidget(
        nodes_ = new props::node_properties(get_current_canv_, parent_)
    );
    layout_->addWidget(ui::horz_separator());
    layout_->addWidget(
        bones_ = new props::bone_properties(get_current_canv_, parent_)
    );
    nodes_->populate(proj);
    bones_->populate(proj);
    nodes_->show();
    bones_->show();
}

void props::mixed_properties::set_selection(const ui::canvas& canv) {
    nodes_->set_selection(canv);
    bones_->set_selection(canv);
}

void props::mixed_properties::lose_selection() {
    nodes_->lose_selection();
    bones_->lose_selection();
}
