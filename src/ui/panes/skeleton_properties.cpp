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

    layout_->addWidget(
        character_ = new ui::labeled_hyperlink("   character", "")
    );
    character_->hide();
    connect(character_->hyperlink(), &QPushButton::clicked, this, [this] {
        auto* selected = get_current_canv_().selected_skeleton();
        if (!selected) return;
        auto parent = selected->model().parent_character();
        if (!parent) return;
        auto& canv = get_current_canv_();
        if (auto* character_item = canv.character_item(parent->get().id()))
            canv.set_selection(character_item, true);
    });

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
        if (auto parent = skel_item->model().parent_character()) {
            character_->hyperlink()->setText(QString::fromStdString(parent->get().name()));
            character_->show();
        } else {
            character_->hide();
        }
    } else {
        character_->hide();
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

    layout_->addWidget(new QLabel("Skeletons"));
    layout_->addWidget(skeletons_ = new QScrollArea());
    skeletons_->setObjectName("characterSkeletons");
    skeletons_->setWidgetResizable(true);
    skeletons_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    skeletons_->setFrameShape(QFrame::NoFrame);

    connect(name_, &QLineEdit::editingFinished, this, [this] {
        if (auto* selected = get_current_canv_().selected_character())
            proj_->rename(selected->id(), name_->text().toStdString());
    });
}

void ui::pane::props::character::set_selection(const canvas::scene& canv) {
    if (auto* selected = canv.selected_character()) {
        name_->setText(QString::fromStdString(selected->model().name()));
        count_->setText(QString("%1 skeleton components").arg(selected->model().rig().size()));

        auto* contents = new QWidget();
        auto* links = new QVBoxLayout(contents);
        links->setContentsMargins(0, 0, 0, 0);
        links->setSpacing(0);
        links->setAlignment(Qt::AlignTop);
        for (auto skel : selected->model().rig().skeletons()) {
            auto* link = new ui::hyperlink_button(QString::fromStdString(skel->name()));
            const auto id = skel->id();
            links->addWidget(link, 0, Qt::AlignLeft);
            connect(link, &QPushButton::clicked, this, [this, id] {
                auto skel = proj_->topology().skeleton(id);
                if (!skel) return;
                get_current_canv_().set_selection(
                    &canvas::item_from_model<canvas::item::skeleton>(skel->get()), true);
            });
        }
        skeletons_->setWidget(contents);
    }
}
