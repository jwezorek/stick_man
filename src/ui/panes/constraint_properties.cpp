#include "constraint_properties.hpp"
#include "properties.hpp"
#include "../canvas/scene.hpp"
#include "../util.hpp"
#include <numbers>

namespace {

QString bone_label(const mdl::project& project, sm::object_id id) {
    auto bone = project.topology().get<sm::bone>(id);
    if (!bone) return "<missing>";
    auto text = QString::fromStdString(bone->get().name());
    auto short_id = QString::fromStdString(id.to_string()).left(8);
    return QString("%1  [%2]").arg(text, short_id);
}

QWidget* labeled_widget(const QString& label, QWidget* value) {
    auto* row = new QWidget;
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(new QLabel(label + ":"));
    layout->addWidget(value, 1);
    return row;
}

} // namespace

ui::pane::props::constraint_properties::constraint_properties(
        const current_canvas_fn& fn, selection_properties* parent) :
    props_box(fn, parent, "Constraint") {}

void ui::pane::props::constraint_properties::populate(mdl::project& proj) {
    layout_->addWidget(name_ = new ui::labeled_field("Name", ""));

    rotation_group_ = new QGroupBox("Rotation Constraint");
    auto* rotation_layout = new QVBoxLayout(rotation_group_);
    target_bone_ = new QLabel;
    rotation_layout->addWidget(labeled_widget("Target Bone", target_bone_));
    reference_ = new QComboBox;
    reference_->addItems({"World", "Parent", "Bone"});
    rotation_layout->addWidget(labeled_widget("Reference", reference_));
    reference_bone_label_ = new QLabel("Reference Bone:");
    reference_bone_ = new QComboBox;
    auto* reference_bone_row = new QWidget;
    auto* reference_bone_layout = new QHBoxLayout(reference_bone_row);
    reference_bone_layout->setContentsMargins(0, 0, 0, 0);
    reference_bone_layout->addWidget(reference_bone_label_);
    reference_bone_layout->addWidget(reference_bone_, 1);
    rotation_layout->addWidget(reference_bone_row);
    rotation_layout->addWidget(range_start_ = new ui::labeled_numeric_val("Range Start", 0, -180, 180, 2));
    rotation_layout->addWidget(range_span_ = new ui::labeled_numeric_val("Range Span", 180, 0, 360, 2));
    layout_->addWidget(rotation_group_);

    triangle_group_ = new QGroupBox("Rigid Triangle Constraint");
    auto* triangle_layout = new QVBoxLayout(triangle_group_);
    first_bone_ = new QLabel;
    second_bone_ = new QLabel;
    triangle_layout->addWidget(labeled_widget("First Bone", first_bone_));
    triangle_layout->addWidget(labeled_widget("Second Bone", second_bone_));
    triangle_layout->addWidget(relative_angle_ = new ui::labeled_numeric_val("Relative Angle", 0, -180, 180, 2));
    layout_->addWidget(triangle_group_);

    connect(name_->value(), &ui::string_edit::value_changed, this, [this](const std::string& name) {
        if (setting_ || !proj_) return;
        if (auto id = get_current_canv_().selected_constraint_id()) proj_->rename(*id, name);
    });
    connect(reference_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int index) {
        if (setting_) return;
        const bool bone_reference = index == 2;
        reference_bone_->setEnabled(bone_reference);
        reference_bone_label_->setEnabled(bone_reference);
        if (bone_reference && reference_bone_->currentIndex() < 0) {
            get_current_canv_().show_status_line("There is no other bone available as a reference.");
            set_selection(get_current_canv_());
            return;
        }
        update_rotation([this, index](sm::rotation_constraint& rotation) {
            if (index == 0) rotation.reference = sm::rotation_reference::world();
            else if (index == 1) rotation.reference = sm::rotation_reference::parent();
            else {
                if (reference_bone_->currentIndex() < 0) return;
                auto id = sm::object_id::from_string(reference_bone_->currentData().toString().toStdString());
                if (id) rotation.reference = sm::rotation_reference::bone(*id);
            }
        });
    });
    connect(reference_bone_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) {
        if (setting_ || reference_->currentIndex() != 2 || reference_bone_->currentIndex() < 0) return;
        auto id = sm::object_id::from_string(reference_bone_->currentData().toString().toStdString());
        if (id) update_rotation([&](sm::rotation_constraint& rotation) {
            rotation.reference = sm::rotation_reference::bone(*id);
        });
    });
    connect(range_start_->num_edit(), &ui::number_edit::value_changed, this, [this](double degrees) {
        if (!setting_) update_rotation([&](sm::rotation_constraint& rotation) {
            rotation.allowed.start_angle = ui::degrees_to_radians(degrees);
        });
    });
    connect(range_span_->num_edit(), &ui::number_edit::value_changed, this, [this](double degrees) {
        if (!setting_) update_rotation([&](sm::rotation_constraint& rotation) {
            rotation.allowed.span_angle = ui::degrees_to_radians(degrees);
        });
    });
    connect(relative_angle_->num_edit(), &ui::number_edit::value_changed, this, [this](double degrees) {
        if (!setting_) update_triangle([&](sm::rigid_triangle_constraint& triangle) {
            triangle.relative_angle = sm::normalize_angle(ui::degrees_to_radians(degrees));
        });
    });
}

void ui::pane::props::constraint_properties::populate_reference_bones(const sm::rotation_constraint& rotation) {
    reference_bone_->clear();
    if (!proj_) return;
    for (auto skel : proj_->topology().skeletons()) {
        for (auto bone : skel->bones()) {
            if (bone->id() == rotation.target_bone) continue;
            reference_bone_->addItem(bone_label(*proj_, bone->id()), QString::fromStdString(bone->id().to_string()));
        }
    }
    if (rotation.reference.kind == sm::rotation_reference_kind::bone) {
        const int index = reference_bone_->findData(QString::fromStdString(rotation.reference.bone_id.to_string()));
        reference_bone_->setCurrentIndex(index);
    }
}

void ui::pane::props::constraint_properties::update_rotation(std::function<void(sm::rotation_constraint&)> edit) {
    auto& canv = get_current_canv_();
    auto id = canv.selected_constraint_id();
    if (!id || !proj_) return;
    auto current = proj_->core().constraint_by_id(*id);
    if (!current || !current->get().rotation()) return;
    auto definition = current->get().definition();
    auto& rotation = std::get<sm::rotation_constraint>(definition);
    edit(rotation);
    const auto result = proj_->update_constraint(*id, definition);
    if (result != sm::result::success) {
        canv.show_status_line("That rotation reference/range is not valid for this constraint.");
        set_selection(canv);
    }
}

void ui::pane::props::constraint_properties::update_triangle(std::function<void(sm::rigid_triangle_constraint&)> edit) {
    auto& canv = get_current_canv_();
    auto id = canv.selected_constraint_id();
    if (!id || !proj_) return;
    auto current = proj_->core().constraint_by_id(*id);
    if (!current || !current->get().triangle()) return;
    auto definition = current->get().definition();
    edit(std::get<sm::rigid_triangle_constraint>(definition));
    const auto result = proj_->update_constraint(*id, definition);
    if (result != sm::result::success) {
        canv.show_status_line("That rigid-triangle angle is not valid.");
        set_selection(canv);
    }
}

void ui::pane::props::constraint_properties::set_selection(const ui::canvas::scene& canv) {
    const auto* constraint = canv.selected_constraint();
    if (!constraint) return;
    setting_ = true;
    name_->set_value(QString::fromStdString(constraint->name()));

    if (auto rotation = constraint->rotation()) {
        set_title("Rotation Constraint");
        rotation_group_->show();
        triangle_group_->hide();
        target_bone_->setText(bone_label(*proj_, rotation->target_bone));
        populate_reference_bones(*rotation);
        reference_->setCurrentIndex(rotation->reference.kind == sm::rotation_reference_kind::world ? 0 :
            rotation->reference.kind == sm::rotation_reference_kind::parent ? 1 : 2);
        const bool bone_reference = rotation->reference.kind == sm::rotation_reference_kind::bone;
        reference_bone_->setEnabled(bone_reference);
        reference_bone_label_->setEnabled(bone_reference);
        range_start_->num_edit()->set_value(ui::radians_to_degrees(rotation->allowed.start_angle));
        range_span_->num_edit()->set_value(ui::radians_to_degrees(rotation->allowed.span_angle));
    } else if (auto triangle = constraint->triangle()) {
        set_title("Rigid Triangle Constraint");
        rotation_group_->hide();
        triangle_group_->show();
        first_bone_->setText(bone_label(*proj_, triangle->first_bone));
        second_bone_->setText(bone_label(*proj_, triangle->second_bone));
        relative_angle_->num_edit()->set_value(ui::radians_to_degrees(triangle->relative_angle));
    }
    setting_ = false;
}

void ui::pane::props::constraint_properties::lose_selection() {}
