#include "skeleton_properties.hpp"
#include "../canvas/scene.hpp"
#include "../canvas/skel_item.hpp"
#include "skeleton_pane.hpp"
#include "main_skeleton_pane.hpp"
#include "../character_actions.hpp"

/*------------------------------------------------------------------------------------------------*/

ui::pane::props::skeletons::skeletons(
        const current_canvas_fn& fn,  selection_properties* parent) :
    props_box(fn, parent, "skeleton selection") {
}

void ui::pane::props::skeletons::populate(mdl::project & proj) {
    make_ = new QPushButton("Make Character");
    layout_->addWidget(make_);
    connect(make_, &QPushButton::clicked, this, [this] {
        character_actions::make(get_current_canv_(), *proj_);
    });
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
        [this](mdl::const_skel_piece piece, const std::string& new_name) {
            handle_rename(piece, name_->value(), new_name);
        }
    );

    connect(
        name_->value(), &ui::string_edit::value_changed,
        this, &skeletons::do_property_name_change
    );
}

void ui::pane::props::skeletons::set_selection(const ui::canvas::scene& canv) {
    make_->setEnabled(!canv.loose_selection().empty());
    auto* skel_item = canv.selected_skeleton();
    name_->setVisible(skel_item != nullptr);
    if (skel_item) {
        set_title("skeleton selection");
        name_->set_value(skel_item->model().name().c_str());
    } else {
        set_title(QString("%1 skeletons selected").arg(canv.selected_skeletons().size()));
    }
}

ui::pane::props::character::character(const current_canvas_fn& fn, selection_properties* parent) :
    props_box(fn, parent, "character selection") {}

void ui::pane::props::character::populate(mdl::project&) {
    layout_->addWidget(new QLabel("Name"));
    layout_->addWidget(name_ = new QLineEdit());
    name_->setObjectName("characterName");
    layout_->addWidget(count_ = new QLabel());
    layout_->addWidget(components_ = new QComboBox());
    components_->setObjectName("characterComponents");
    auto* select = new QPushButton("Select Component");
    layout_->addWidget(select);
    connect(select, &QPushButton::clicked, this, [this] {
        auto id = sm::object_id::from_string(components_->currentData().toString().toStdString());
        if (!id) return;
        auto skel = proj_->topology().skeleton(*id);
        if (skel) get_current_canv_().set_selection(&canvas::item_from_model<canvas::item::skeleton>(skel->get()), true);
    });
    connect(name_, &QLineEdit::editingFinished, this, [this] {
        if (auto* selected = get_current_canv_().selected_character())
            proj_->rename(selected->id(), name_->text().toStdString());
    });
}

void ui::pane::props::character::set_selection(const canvas::scene& canv) {
    if (auto* selected = canv.selected_character()) {
        name_->setText(QString::fromStdString(selected->model().name()));
        count_->setText(QString("%1 skeleton components").arg(selected->model().rig().size()));
        components_->clear();
        for (auto skel : selected->model().rig().skeletons())
            components_->addItem(QString::fromStdString(skel->name()), QString::fromStdString(skel->id().to_string()));
    }
}
