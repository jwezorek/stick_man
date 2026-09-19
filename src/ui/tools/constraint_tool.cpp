#include "constraint_tool.hpp"
#include "../canvas/bone_item.hpp"
#include "../canvas/node_item.hpp"
#include "../../model/handle.hpp"
#include "../../model/project.hpp"
#include <numbers>

namespace {
    constexpr double k_default_rot_constraint_min = -std::numbers::pi / 2.0;
    constexpr double k_default_rot_constraint_span = std::numbers::pi;
}

/*------------------------------------------------------------------------------------------------*/

ui::tool::constraint::constraint() :
    settings_(nullptr),
    absolute_constraint_(nullptr),
    model_(nullptr),
    base("constraint", "push_pin_icon.png", ui::tool::id::constraint) {
}

void ui::tool::constraint::init(canvas::manager&, mdl::project& model) {
    model_ = &model;

    settings_ = new QWidget();
    auto* layout = new QVBoxLayout(settings_);
    layout->setAlignment(Qt::AlignTop);
    layout->addWidget(absolute_constraint_ = new QCheckBox("absolute constraint"));
    absolute_constraint_->setToolTip(
        "When unchecked, new bone rotation constraints are relative to the parent bone."
    );
}

void ui::tool::constraint::mouseReleaseEvent(
        canvas::scene& canv, QGraphicsSceneMouseEvent* event) {
    if (event->button() != Qt::LeftButton) {
        return;
    }

    auto* item = canv.top_item(event->scenePos());
    if (!item) {
        return;
    }

    if (auto* node = dynamic_cast<canvas::item::node*>(item)) {
        canv.toggle_node_pinned(node->model().id());
        return;
    }

    auto* bone = dynamic_cast<canvas::item::bone*>(item);
    if (!bone || bone->model().rotation_constraint()) {
        return;
    }

    const bool relative_to_parent = !absolute_constraint_->isChecked();
    if (relative_to_parent && !bone->model().parent_bone()) {
        canv.show_status_line(
            "A root bone has no parent. Enable absolute constraint to constrain it."
        );
        return;
    }

    model_->transform(
        std::vector<mdl::handle>{ mdl::to_handle(bone->model()) },
        [relative_to_parent](sm::bone& model_bone) {
            model_bone.set_rotation_constraint(
                k_default_rot_constraint_min,
                k_default_rot_constraint_span,
                relative_to_parent
            );
        }
    );
}

QWidget* ui::tool::constraint::settings_widget() {
    return settings_;
}
