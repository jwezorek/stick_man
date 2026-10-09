#include "skeleton_properties.hpp"
#include "properties.hpp"
#include "../canvas/scene.hpp"
#include "../canvas/skel_item.hpp"
#include "../character_actions.hpp"
#include <functional>
#include <QSignalBlocker>

/*------------------------------------------------------------------------------------------------*/

ui::pane::props::skeletons::skeletons(
        const current_canvas_fn& fn,  properties_widget* parent) :
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
            return parent_->validate_props_name_change(new_name);
        }
    );

    layout_->addWidget(
        character_ = new ui::labeled_hyperlink("   character", "")
    );
    character_->hide();
    connect(character_->hyperlink(), &QPushButton::clicked, this, [this] {
        auto* selected = get_current_canv_().selected_skeleton();
        if (!selected)
            return;
        auto parent = selected->model().parent_character();
        if (!parent)
            return;
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

ui::pane::props::character::character(const current_canvas_fn& fn, properties_widget* parent) :
    props_box(fn, parent, "character selection") {}

void ui::pane::props::character::populate(mdl::project& proj) {
    layout_->addWidget(new QLabel("Name"));
    layout_->addWidget(name_ = new QLineEdit());
    name_->setObjectName("characterName");
    layout_->addWidget(count_ = new QLabel());

    layout_->addWidget(new QLabel("Character Root Bone"));
    layout_->addWidget(root_bone_ = new QComboBox());
    root_bone_->setObjectName("characterRootBone");

    layout_->addWidget(new QLabel("Skeletons"));
    layout_->addWidget(skeletons_ = new QScrollArea());
    skeletons_->setObjectName("characterSkeletons");
    skeletons_->setWidgetResizable(true);
    skeletons_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    skeletons_->setFrameShape(QFrame::NoFrame);

    connect(name_, &QLineEdit::editingFinished, this, [this] {
        if (parent_->read_only())
            return;
        if (auto* selected = get_current_canv_().selected_character())
            proj_->rename(selected->id(), name_->text().toStdString());
    });
    connect(root_bone_, QOverload<int>::of(&QComboBox::activated), this, [this](int index) {
        if (parent_->read_only() || index < 0)
            return;
        auto* selected = get_current_canv_().selected_character();
        if (!selected)
            return;
        const auto parsed = sm::object_id::from_string(root_bone_->itemData(index).toString().toStdString());
        if (!parsed)
            return;
        if (proj_->set_character_root_bone(selected->id(), *parsed) != sm::result::success)
            set_selection(get_current_canv_());
    });
    // Also update the displayed choice on undo/redo and after structural edits
    // that replace the selected bone.
    connect(&proj, &mdl::project::project_changed, this, [this] {
        if (get_current_canv_().selected_character())
            set_selection(get_current_canv_());
    });
}

void ui::pane::props::character::set_selection(const canvas::scene& canv) {
    if (auto* selected = canv.selected_character()) {
        name_->setText(QString::fromStdString(selected->model().name()));
        count_->setText(QString("%1 skeleton components").arg(selected->model().rig().size()));

        // Display bones in rooted hierarchy order, not in the skeleton's
        // unordered ID lookup table. The rig's skeleton order is preserved.
        const QSignalBlocker block(root_bone_);
        root_bone_->clear();
        const bool multiple = selected->model().rig().size() > 1;
        for (auto skel : selected->model().rig().skeletons()) {
            const auto add_bone = [this, multiple, skel](const auto& self, const sm::bone& bone) -> void {
                const auto label = QString::fromStdString(bone.name());
                root_bone_->addItem(multiple
                    ? QString::fromStdString(skel->name()) + " / " + label : label,
                    QString::fromStdString(bone.id().to_string()));
                for (auto child : bone.child_bones())
                    self(self, child.get());
            };
            for (auto bone : skel->root_node().child_bones())
                add_bone(add_bone, bone.get());
        }
        const auto root = selected->model().character_root_bone();
        if (root) root_bone_->setCurrentIndex(
            root_bone_->findData(QString::fromStdString(root->to_string())));
        if (root_bone_->count() == 0)
            root_bone_->addItem("(no bones)");
        root_bone_->setEnabled(root_bone_->count() > 0 && root.has_value() && !parent_->read_only());


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
                if (!skel)
                    return;
                get_current_canv_().set_selection(
                    &canvas::item_from_model<canvas::item::skeleton>(skel->get()), true);
            });
        }
        skeletons_->setWidget(contents);
    }
}
