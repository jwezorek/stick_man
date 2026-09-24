#include "constraint_tool.hpp"
#include "../canvas/bone_item.hpp"
#include "../canvas/node_item.hpp"
#include "../canvas/canvas_manager.hpp"
#include "../util.hpp"
#include "../../model/project.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace {
constexpr double k_default_rot_constraint_min = -std::numbers::pi / 2.0;
constexpr double k_default_rot_constraint_span = std::numbers::pi;
constexpr double tau = 2.0 * std::numbers::pi;

double positive_span(double start, double end) {
    double span = std::fmod(end - start, tau);
    if (span < 0) span += tau;
    return span;
}

double reference_angle(const sm::topology& topology, const sm::rotation_constraint& rotation) {
    auto target = topology.get<sm::bone>(rotation.target_bone);
    if (!target) return 0.0;
    switch (rotation.reference.kind) {
    case sm::rotation_reference_kind::world:
        return 0.0;
    case sm::rotation_reference_kind::parent:
        if (auto parent = target->get().parent_bone()) return parent->get().world_rotation();
        return 0.0;
    case sm::rotation_reference_kind::bone:
        if (auto bone = topology.get<sm::bone>(rotation.reference.bone_id)) return bone->get().world_rotation();
        return 0.0;
    }
    return 0.0;
}

QString result_message(sm::result result) {
    switch (result) {
    case sm::result::no_parent: return "The target bone has no parent bone.";
    case sm::result::not_found: return "A referenced bone no longer exists.";
    case sm::result::invalid_constraint: return "That constraint relationship is invalid.";
    case sm::result::inconsistent_constraints: return "That constraint conflicts with an existing rigid relationship.";
    case sm::result::invalid_membership: return "Project constraints cannot be edited while Animation Mode is active.";
    default: return "The constraint edit was rejected by Core validation.";
    }
}
}

ui::tool::constraint::constraint() :
    base("constraint", "push_pin_icon.png", ui::tool::id::constraint) {}

ui::tool::constraint::operation ui::tool::constraint::current_operation() const {
    return static_cast<operation>(std::max(0, operation_->currentIndex()));
}

sm::rotation_reference_kind ui::tool::constraint::current_reference_kind() const {
    return static_cast<sm::rotation_reference_kind>(std::max(0, reference_->currentIndex()));
}

void ui::tool::constraint::update_settings_state() {
    const bool rotation = current_operation() == operation::rotation;
    reference_->setEnabled(rotation);
    reference_label_->setEnabled(rotation);
}

void ui::tool::constraint::activate(canvas::manager& canvases) {
    canvases_ = &canvases;
    for (auto* canv : canvases.canvases()) {
        canv->set_constraint_tool_active(true);
        canv->sync_to_model();
    }
}

void ui::tool::constraint::deactivate(canvas::manager& canvases) {
    if (drag_) cancel_drag(canvases.active_canvas());
    clear_triangle_sweep();
    clear_pending();
    for (auto* canv : canvases.canvases()) {
        canv->set_hovered_constraint({});
        canv->clear_constraint_selection();
        canv->set_constraint_tool_active(false);
    }
}

void ui::tool::constraint::init(canvas::manager& canvases, mdl::project& model) {
    model_ = &model;
    canvases_ = &canvases;

    settings_ = new QWidget();
    auto* layout = new QVBoxLayout(settings_);
    layout->setAlignment(Qt::AlignTop);

    operation_ = new QComboBox;
    operation_->setObjectName("constraint_operation");
    operation_->addItems({"Select / Edit", "Rotation", "Rigid Triangle"});
    auto* operation_row = new QHBoxLayout;
    operation_row->addWidget(new QLabel("Operation:"));
    operation_row->addWidget(operation_, 1);
    layout->addLayout(operation_row);

    reference_label_ = new QLabel("Rotation reference:");
    reference_ = new QComboBox;
    reference_->setObjectName("constraint_create_reference");
    reference_->addItems({"World", "Parent", "Bone"});
    reference_->setCurrentIndex(int(sm::rotation_reference_kind::parent));
    layout->addWidget(reference_label_);
    layout->addWidget(reference_);
    layout->addWidget(new QLabel(
        "Select/Edit: click adornments; drag handles.\n"
        "Nodes: click to pin/unpin in every operation.\n"
        "Bone reference: click target, then reference bone.\n"
        "Rigid Triangle: click two sibling bones, or drag\n"
        "from empty space through both siblings."));
    layout->addStretch();

    QObject::connect(operation_, qOverload<int>(&QComboBox::currentIndexChanged), settings_, [this](int) {
        if (canvases_) {
            if (drag_) cancel_drag(canvases_->active_canvas());
            clear_triangle_sweep();
            clear_pending();
        }
        update_settings_state();
    });
    QObject::connect(reference_, qOverload<int>(&QComboBox::currentIndexChanged), settings_, [this](int) {
        clear_pending();
    });
    QObject::connect(&model, &mdl::project::new_project_opened, settings_, [this](mdl::project&) {
        // Creation previews are editor state and must not survive a document swap.
        clear_triangle_sweep();
        clear_pending();
        drag_.reset();
    });
    update_settings_state();
}

void ui::tool::constraint::clear_pending() {
    pending_bone_.reset();
    if (pending_highlight_ && pending_scene_) {
        pending_scene_->removeItem(pending_highlight_);
        delete pending_highlight_;
    }
    pending_highlight_ = nullptr;
    if (pending_scene_ && pending_scene_->is_status_line_visible()) pending_scene_->hide_status_line();
    pending_scene_ = nullptr;
}

void ui::tool::constraint::show_pending(canvas::scene& canv, const sm::bone& bone, const QString& message) {
    clear_pending();
    pending_bone_ = bone.id();
    pending_scene_ = &canv;
    pending_highlight_ = new QGraphicsLineItem;
    auto [root, tip] = bone.line_segment();
    pending_highlight_->setLine(QLineF(ui::to_qt_pt(root), ui::to_qt_pt(tip)));
    QPen pen(canvas::k_sel_color, 5.0, Qt::DashLine, Qt::RoundCap);
    pen.setCosmetic(true);
    pending_highlight_->setPen(pen);
    pending_highlight_->setZValue(10000);
    canv.addItem(pending_highlight_);
    canv.show_status_line(message);
}

void ui::tool::constraint::clear_triangle_sweep(bool hide_status) {
    if (!triangle_sweep_) return;
    auto* scene = triangle_sweep_->scene;
    if (triangle_sweep_->trail && scene) {
        scene->removeItem(triangle_sweep_->trail);
        delete triangle_sweep_->trail;
    }
    if (triangle_sweep_->first_highlight && scene) {
        scene->removeItem(triangle_sweep_->first_highlight);
        delete triangle_sweep_->first_highlight;
    }
    if (hide_status && scene && scene->is_status_line_visible()) scene->hide_status_line();
    triangle_sweep_.reset();
}

void ui::tool::constraint::begin_triangle_sweep(canvas::scene& canv, QPointF point) {
    clear_triangle_sweep();
    clear_pending();

    triangle_sweep_state state;
    state.scene = &canv;
    state.last_point = point;
    state.trail = new QGraphicsPathItem;
    QPainterPath path(point);
    state.trail->setPath(path);
    QPen pen(canvas::k_sel_color, 2.0, Qt::DashLine, Qt::RoundCap, Qt::RoundJoin);
    pen.setCosmetic(true);
    state.trail->setPen(pen);
    state.trail->setZValue(9999);
    canv.addItem(state.trail);
    triangle_sweep_ = state;
    canv.show_status_line("Rigid Triangle: sweep through two sibling bones (Esc cancels).");
}

void ui::tool::constraint::set_triangle_sweep_first(canvas::scene& canv, const sm::bone& bone) {
    if (!triangle_sweep_) return;
    if (triangle_sweep_->first_highlight) {
        canv.removeItem(triangle_sweep_->first_highlight);
        delete triangle_sweep_->first_highlight;
    }

    triangle_sweep_->first_bone = bone.id();
    triangle_sweep_->first_highlight = new QGraphicsLineItem;
    auto [root, tip] = bone.line_segment();
    triangle_sweep_->first_highlight->setLine(QLineF(ui::to_qt_pt(root), ui::to_qt_pt(tip)));
    QPen pen(canvas::k_sel_color, 5.0, Qt::DashLine, Qt::RoundCap);
    pen.setCosmetic(true);
    triangle_sweep_->first_highlight->setPen(pen);
    triangle_sweep_->first_highlight->setZValue(10000);
    canv.addItem(triangle_sweep_->first_highlight);
    canv.show_status_line("Rigid Triangle: sweep through a sibling of the highlighted bone.");
}

void ui::tool::constraint::process_triangle_sweep_bone(canvas::scene& canv, sm::bone& bone) {
    if (!triangle_sweep_) return;
    if (!triangle_sweep_->first_bone) {
        set_triangle_sweep_first(canv, bone);
        return;
    }

    auto first = model_->topology().get<sm::bone>(*triangle_sweep_->first_bone);
    if (!first) {
        clear_triangle_sweep(false);
        report_failure(canv, sm::result::not_found, "Cannot create rigid triangle");
        return;
    }
    if (first->get().id() == bone.id()) return;

    if (!first->get().is_sibling(bone)) {
        // Keep the gesture forgiving: the most recently crossed unrelated bone
        // becomes the first candidate, so any consecutive sibling pair can win.
        set_triangle_sweep_first(canv, bone);
        return;
    }

    const auto first_id = first->get().id();
    const auto second_id = bone.id();
    auto result = add_triangle(canv, first_id, second_id);
    if (!result) {
        clear_triangle_sweep(false);
        return;
    }

    const auto constraint_id = *result;
    clear_triangle_sweep();
    canv.select_constraint(constraint_id);
}

void ui::tool::constraint::update_triangle_sweep(canvas::scene& canv, QPointF point) {
    if (!triangle_sweep_ || triangle_sweep_->scene != &canv) return;

    auto path = triangle_sweep_->trail->path();
    path.lineTo(point);
    triangle_sweep_->trail->setPath(path);

    const QPointF start = triangle_sweep_->last_point;
    const QLineF motion(start, point);
    const double screen_length = motion.length() * std::max(0.01, canv.scale());
    const int steps = std::clamp(static_cast<int>(std::ceil(screen_length / 3.0)), 1, 512);

    for (int i = 1; i <= steps && triangle_sweep_; ++i) {
        const double t = static_cast<double>(i) / static_cast<double>(steps);
        const QPointF sample = start + (point - start) * t;
        auto* item = canv.top_item(sample);
        auto* bone = dynamic_cast<canvas::item::bone*>(item);
        if (!bone) {
            triangle_sweep_->last_crossed_bone.reset();
            continue;
        }

        const auto id = bone->model().id();
        if (triangle_sweep_->last_crossed_bone && *triangle_sweep_->last_crossed_bone == id) continue;
        triangle_sweep_->last_crossed_bone = id;
        process_triangle_sweep_bone(canv, bone->model());
    }

    if (triangle_sweep_) triangle_sweep_->last_point = point;
}

void ui::tool::constraint::report_failure(canvas::scene& canv, sm::result result, const QString& action) {
    canv.show_status_line(action + ": " + result_message(result));
}

void ui::tool::constraint::create_rotation(canvas::scene& canv, sm::bone& target) {
    const auto kind = current_reference_kind();
    if (kind == sm::rotation_reference_kind::bone) {
        if (!pending_bone_) {
            show_pending(canv, target, "Rotation constraint: click the reference bone (Esc cancels).");
            return;
        }
        auto target_id = *pending_bone_;
        auto result = model_->add_rotation_constraint(target_id, sm::rotation_reference::bone(target.id()),
            {k_default_rot_constraint_min, k_default_rot_constraint_span});
        if (!result) {
            report_failure(canv, result.error(), "Cannot create rotation constraint");
            return;
        }
        clear_pending();
        canv.select_constraint(*result);
        return;
    }

    const auto reference = kind == sm::rotation_reference_kind::parent
        ? sm::rotation_reference::parent() : sm::rotation_reference::world();
    auto result = model_->add_rotation_constraint(target.id(), reference,
        {k_default_rot_constraint_min, k_default_rot_constraint_span});
    if (!result) {
        report_failure(canv, result.error(), "Cannot create rotation constraint");
        return;
    }
    if (canv.is_status_line_visible()) canv.hide_status_line();
    canv.select_constraint(*result);
}

std::optional<sm::object_id> ui::tool::constraint::add_triangle(
    canvas::scene& canv, sm::object_id first_id, sm::object_id second_id) {
    auto first = model_->topology().get<sm::bone>(first_id);
    auto second = model_->topology().get<sm::bone>(second_id);
    if (!first || !second) {
        report_failure(canv, sm::result::not_found, "Cannot create rigid triangle");
        return std::nullopt;
    }
    if (first_id == second_id) {
        canv.show_status_line("Rigid Triangle: choose a different second bone.");
        return std::nullopt;
    }
    if (!first->get().is_sibling(second->get())) {
        canv.show_status_line("Rigid Triangle: second bone must be a sibling with the same root.");
        return std::nullopt;
    }

    auto result = model_->add_rigid_triangle_constraint(first_id, second_id);
    if (!result) {
        report_failure(canv, result.error(), "Cannot create rigid triangle");
        return std::nullopt;
    }
    return *result;
}

void ui::tool::constraint::create_triangle(canvas::scene& canv, sm::bone& bone) {
    if (!pending_bone_) {
        show_pending(canv, bone, "Rigid Triangle: click a sibling bone with the same root (Esc cancels).");
        return;
    }

    auto result = add_triangle(canv, *pending_bone_, bone.id());
    if (!result) return;
    clear_pending();
    canv.select_constraint(*result);
}

void ui::tool::constraint::keyPressEvent(canvas::scene& canv, QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape) {
        if (drag_) cancel_drag(canv);
        clear_triangle_sweep();
        clear_pending();
        canv.set_hovered_constraint({});
    }
}

void ui::tool::constraint::mousePressEvent(canvas::scene& canv, QGraphicsSceneMouseEvent* event) {
    press_handled_ = false;
    if (event->button() != Qt::LeftButton) return;

    auto* item = canv.top_item(event->scenePos());

    // Node clicks always mean pin/unpin for the constraint tool.  Give nodes
    // priority over any constraint adornment that happens to overlap them.
    if (dynamic_cast<canvas::item::node*>(item)) return;

    if (auto hit = canv.constraint_at(event->scenePos())) {
        clear_triangle_sweep();
        clear_pending();
        canv.select_constraint(hit->id);
        press_handled_ = true;
        if (hit->part != canvas::constraint_part::body) {
            auto current = model_->core().constraint_by_id(hit->id);
            if (current) drag_ = drag_state{hit->id, hit->part, current->get().definition()};
        }
        return;
    }

    if (current_operation() == operation::rigid_triangle && !item) {
        begin_triangle_sweep(canv, event->scenePos());
        press_handled_ = true;
    }
}

void ui::tool::constraint::update_drag(canvas::scene& canv, QPointF point) {
    if (!drag_) return;
    auto current = model_->core().constraint_by_id(drag_->id);
    if (!current) { drag_.reset(); return; }
    auto definition = current->get().definition();

    if (auto* rotation = std::get_if<sm::rotation_constraint>(&definition)) {
        auto target = model_->topology().get<sm::bone>(rotation->target_bone);
        if (!target) return;
        auto pivot = target->get().parent_node().world_pos();
        if (rotation->reference.kind == sm::rotation_reference_kind::world) {
            const auto tip = target->get().child_node().world_pos();
            pivot = {(pivot.x + tip.x) / 2.0, (pivot.y + tip.y) / 2.0};
        }
        const double world_angle = sm::angle_from_u_to_v(pivot, ui::from_qt_pt(point));
        const double local_angle = sm::normalize_angle(world_angle - reference_angle(model_->topology(), *rotation));
        if (drag_->part == canvas::constraint_part::rotation_min) {
            const double old_end = rotation->allowed.start_angle + rotation->allowed.span_angle;
            rotation->allowed.start_angle = local_angle;
            rotation->allowed.span_angle = positive_span(local_angle, old_end);
        } else if (drag_->part == canvas::constraint_part::rotation_max) {
            rotation->allowed.span_angle = positive_span(rotation->allowed.start_angle, local_angle);
        } else return;
    } else if (auto* triangle = std::get_if<sm::rigid_triangle_constraint>(&definition)) {
        if (drag_->part != canvas::constraint_part::triangle_angle) return;
        auto first = model_->topology().get<sm::bone>(triangle->first_bone);
        if (!first) return;
        const auto pivot = first->get().parent_node().world_pos();
        const double world_angle = sm::angle_from_u_to_v(pivot, ui::from_qt_pt(point));
        triangle->relative_angle = sm::normalize_angle(world_angle - first->get().world_rotation());
    }

    const auto result = model_->core().update_constraint(drag_->id, definition);
    if (result == sm::result::success)
        canv.sync_to_model();
    else
        report_failure(canv, result, "Cannot preview constraint edit");
}

void ui::tool::constraint::cancel_drag(canvas::scene& canv) {
    if (!drag_) return;
    model_->core().update_constraint(drag_->id, drag_->original);
    drag_.reset();
    canv.sync_to_model();
}

void ui::tool::constraint::finish_drag(canvas::scene& canv) {
    if (!drag_) return;
    auto current = model_->core().constraint_by_id(drag_->id);
    if (!current) { drag_.reset(); return; }
    const auto id = drag_->id;
    const auto before = drag_->original;
    const auto after = current->get().definition();
    model_->core().update_constraint(id, before);
    drag_.reset();
    const auto result = model_->update_constraint(id, after);
    if (result != sm::result::success) {
        model_->core().update_constraint(id, before);
        report_failure(canv, result, "Cannot edit constraint");
    }
    canv.select_constraint(id);
    canv.sync_to_model();
}

void ui::tool::constraint::mouseMoveEvent(canvas::scene& canv, QGraphicsSceneMouseEvent* event) {
    if (drag_) {
        update_drag(canv, event->scenePos());
        return;
    }
    if (triangle_sweep_) {
        update_triangle_sweep(canv, event->scenePos());
        return;
    }
    auto hit = canv.constraint_at(event->scenePos());
    canv.set_hovered_constraint(hit ? std::optional<sm::object_id>{hit->id} : std::nullopt);

    if (pending_bone_) {
        if (auto* item = canv.top_item(event->scenePos()); auto* bone = dynamic_cast<canvas::item::bone*>(item)) {
            if (current_operation() == operation::rigid_triangle) {
                auto first = model_->topology().get<sm::bone>(*pending_bone_);
                const bool valid = first && first->get().id() != bone->model().id() &&
                    first->get().is_sibling(bone->model());
                canv.show_status_line(valid ? "Rigid Triangle: click to create." :
                    "Rigid Triangle: second bone must be a different sibling with the same root.");
            }
        }
    }
}

void ui::tool::constraint::mouseReleaseEvent(canvas::scene& canv, QGraphicsSceneMouseEvent* event) {
    if (event->button() != Qt::LeftButton) return;
    if (drag_) {
        update_drag(canv, event->scenePos());
        finish_drag(canv);
        press_handled_ = false;
        return;
    }
    if (triangle_sweep_) {
        update_triangle_sweep(canv, event->scenePos());
        clear_triangle_sweep();
        press_handled_ = false;
        return;
    }
    if (press_handled_) {
        press_handled_ = false;
        return;
    }

    auto* item = canv.top_item(event->scenePos());
    if (!item) {
        if (current_operation() == operation::select) canv.clear_constraint_selection();
        return;
    }
    if (auto* node = dynamic_cast<canvas::item::node*>(item)) {
        canv.toggle_node_pinned_undoable(node->model().id());
        return;
    }
    auto* bone = dynamic_cast<canvas::item::bone*>(item);
    if (!bone) return;

    switch (current_operation()) {
    case operation::select:
        return;
    case operation::rotation:
        create_rotation(canv, bone->model());
        return;
    case operation::rigid_triangle:
        create_triangle(canv, bone->model());
        return;
    }
}

QWidget* ui::tool::constraint::settings_widget() { return settings_; }
