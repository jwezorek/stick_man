#include "../../core/sm_constraint.hpp"
#include "constraint_tool.hpp"
#include "../canvas/bone_item.hpp"
#include "../canvas/node_item.hpp"
#include "../canvas/canvas_manager.hpp"
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

void ui::tool::constraint::activate(canvas::manager& canvases) {
    for (auto* canv : canvases.canvases()) {
        canv->set_rotation_constraints_visible(true);
    }
}

void ui::tool::constraint::deactivate(canvas::manager& canvases) {
    for (auto* canv : canvases.canvases()) {
        canv->set_rotation_constraints_visible(false);
    }
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

    if (model_->animation_mode()) {
        // TODO: In Animation Mode, bone constraints should be authored as constraints
        // local to the selected animation action rather than modifying project topology.
        return;
    }

    auto* bone = dynamic_cast<canvas::item::bone*>(item);
    if (!bone || sm::editor_rotation_constraint(bone->model())) {
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
            sm::set_editor_rotation_constraint(model_bone, 
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
