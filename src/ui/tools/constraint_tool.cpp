#include "constraint_tool.hpp"
#include "../canvas/bone_item.hpp"
#include "../canvas/node_item.hpp"
#include "../canvas/canvas_manager.hpp"
#include "../util.hpp"
#include "../../model/project.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>
#include <limits>
#include <QSignalBlocker>
#include <QStandardItemModel>
#include <QPainterPath>

namespace {
constexpr double k_default_rot_constraint_min = -std::numbers::pi / 2.0;
constexpr double k_default_rot_constraint_span = std::numbers::pi;
constexpr double tau = 2.0 * std::numbers::pi;

double positive_span(double start, double end) {
    double span = std::fmod(end - start, tau);
    if (span < 0)
        span += tau;
    return span;
}

double reference_angle(const sm::topology& topology, const sm::rotation_constraint& rotation) {
    auto target = topology.get<sm::bone>(rotation.target_bone);
    if (!target)
        return 0.0;
    switch (rotation.reference.kind) {
    case sm::rotation_reference_kind::world:
        return 0.0;
    case sm::rotation_reference_kind::parent:
        if (auto parent = target->get().parent_bone())
            return parent->get().world_rotation();
        return 0.0;
    case sm::rotation_reference_kind::bone:
        if (auto bone = topology.get<sm::bone>(rotation.reference.bone_id))
            return bone->get().world_rotation();
        return 0.0;
    }
    return 0.0;
}

QString result_message(sm::result result) {
    switch (result) {
    case sm::result::no_parent:
        return "The target bone has no parent bone.";
    case sm::result::not_found:
        return "A referenced bone no longer exists.";
    case sm::result::invalid_constraint:
        return "That constraint relationship is invalid.";
    case sm::result::inconsistent_constraints:
        return "That constraint conflicts with an existing rigid relationship.";
    case sm::result::invalid_membership:
        return "Project constraints cannot be edited while Animation Mode is active.";
    default:
        return "The constraint edit was rejected by Core validation.";
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
    if (!model_)
        return;
    const bool rotation = current_operation() == operation::rotation;
    if (operation_) {
        operation_->setEnabled(true);
        if (auto* entries = qobject_cast<QStandardItemModel*>(operation_->model()))
            entries->item(int(operation::path))->setEnabled(model_->animation_has_outgoing_transition());
        if (current_operation() == operation::path && !model_->animation_has_outgoing_transition())
            operation_->setCurrentIndex(int(operation::select));
    }
    reference_->setEnabled(rotation);
    reference_label_->setEnabled(rotation);
}

void ui::tool::constraint::activate(canvas::manager& canvases) {
    canvases_ = &canvases;
    active_ = true;
    for (auto* canv : canvases.canvases()) {
        canv->set_constraint_tool_active(true);
        canv->sync_to_model();
    }
    redraw_paths(canvases.active_canvas());
}

void ui::tool::constraint::deactivate(canvas::manager& canvases) {
    active_ = false;
    path_gesture_.reset();
    selected_path_.reset();
    clear_path_graphics();
    if (drag_)
        cancel_drag(canvases.active_canvas());
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
    operation_->addItems({"Select / Edit", "Rotation", "Rigid Triangle", "Path"});
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
        "from empty space through both siblings.\n"
        "Path: click a node (drag to bend curves).\n"
        "Drag handles; double-click a path to add a knot.\n"
        "Right-click path to insert or knot to remove.\n"
        "Delete removes the selected path."));
    layout->addStretch();

    QObject::connect(operation_, qOverload<int>(&QComboBox::currentIndexChanged), settings_, [this](int) {
        if (canvases_) {
            if (drag_)
                cancel_drag(canvases_->active_canvas());
            clear_triangle_sweep();
            clear_pending();
        }
        update_settings_state();
    });
    QObject::connect(&model, &mdl::project::animation_keyframe_selected, settings_, [this](sm::object_id) {
        selected_path_.reset();
        path_gesture_.reset();
        if (active_ && canvases_)
            redraw_paths(canvases_->active_canvas());
        update_settings_state();
    });
    QObject::connect(&model, &mdl::project::refresh_canvas, settings_, [this](mdl::project&, bool) {
        if (active_ && canvases_ && !path_gesture_)
            redraw_paths(canvases_->active_canvas());
        update_settings_state();
    });
    QObject::connect(reference_, qOverload<int>(&QComboBox::currentIndexChanged), settings_, [this](int) {
        clear_pending();
        if (!model_ || !model_->animation_mode() || !canvases_)
            return;
        auto& canv = canvases_->active_canvas();
        auto selected = canv.selected_constraint_id();
        if (!selected)
            return;
        auto current = model_->animation_session_rotation_constraint(*selected);
        if (!current)
            return;
        const auto kind = current_reference_kind();
        if (kind == current->reference.kind)
            return;
        if (kind == sm::rotation_reference_kind::bone) {
            auto target = model_->topology().get<sm::bone>(current->target_bone);
            if (!target)
                return;
            show_pending(canv, target->get(),
                "Rotation constraint: click the reference bone (Esc cancels).");
            pending_edit_constraint_ = *selected;
            return;
        }
        current->reference = kind == sm::rotation_reference_kind::parent
            ? sm::rotation_reference::parent() : sm::rotation_reference::world();
        const auto result = model_->update_animation_rotation_constraint(*selected, *current);
        if (result != sm::result::success)
            report_failure(canv, result, "Cannot change transition rotation reference");
        canv.sync_to_model();
    });
    QObject::connect(&model, &mdl::project::new_project_opened, settings_, [this](mdl::project&) {
        // Creation previews are editor state and must not survive a document swap.
        clear_triangle_sweep();
        clear_pending();
        drag_.reset();
        path_gesture_.reset();
        selected_path_.reset();
        clear_path_graphics();
    });
    update_settings_state();
}

void ui::tool::constraint::set_animation_mode(bool) {
    selected_path_.reset();
    path_gesture_.reset();
    clear_path_graphics();
    if (!settings_)
        return;
    if (canvases_) {
        if (drag_)
            cancel_drag(canvases_->active_canvas());
        clear_triangle_sweep();
        clear_pending();
        for (auto* canv : canvases_->canvases()) {
            canv->set_hovered_constraint({});
            canv->clear_constraint_selection();
        }
    }
    update_settings_state();
    if (active_ && canvases_)
        redraw_paths(canvases_->active_canvas());
}

void ui::tool::constraint::clear_pending() {
    pending_bone_.reset();
    pending_edit_constraint_.reset();
    if (pending_highlight_ && pending_scene_) {
        pending_scene_->removeItem(pending_highlight_);
        delete pending_highlight_;
    }
    pending_highlight_ = nullptr;
    if (pending_scene_ && pending_scene_->is_status_line_visible())
        pending_scene_->hide_status_line();
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
    if (!triangle_sweep_)
        return;
    auto* scene = triangle_sweep_->scene;
    if (triangle_sweep_->trail && scene) {
        scene->removeItem(triangle_sweep_->trail);
        delete triangle_sweep_->trail;
    }
    if (triangle_sweep_->first_highlight && scene) {
        scene->removeItem(triangle_sweep_->first_highlight);
        delete triangle_sweep_->first_highlight;
    }
    if (hide_status && scene && scene->is_status_line_visible())
        scene->hide_status_line();
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
    if (!triangle_sweep_)
        return;
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
    if (!triangle_sweep_)
        return;
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
    if (first->get().id() == bone.id())
        return;

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
    if (!triangle_sweep_ || triangle_sweep_->scene != &canv)
        return;

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
        if (triangle_sweep_->last_crossed_bone && *triangle_sweep_->last_crossed_bone == id)
            continue;
        triangle_sweep_->last_crossed_bone = id;
        process_triangle_sweep_bone(canv, bone->model());
    }

    if (triangle_sweep_)
        triangle_sweep_->last_point = point;
}

void ui::tool::constraint::report_failure(canvas::scene& canv, sm::result result, const QString& action) {
    canv.show_status_line(action + ": " + result_message(result));
}

void ui::tool::constraint::create_rotation(canvas::scene& canv, sm::bone& target) {
    const auto kind = current_reference_kind();
    const bool animation = model_ && model_->animation_mode();
    if (animation && !pending_bone_) {
        for (const auto& [id, c] : model_->animation_session_rotation_constraints()) {
            if (c.rotation() && c.rotation()->target_bone == target.id()) {
                const auto result = model_->remove_animation_rotation_constraint(id);
                if (result != sm::result::success)
                    report_failure(canv, result, "Cannot remove transition rotation constraint");
                else
                    canv.sync_to_model();
                return;
            }
        }
    }
    if (kind == sm::rotation_reference_kind::bone) {
        if (!pending_bone_) {
            show_pending(canv, target, "Rotation constraint: click the reference bone (Esc cancels).");
            return;
        }
        auto target_id = *pending_bone_;
        if (animation && pending_edit_constraint_) {
            const auto edit_id = *pending_edit_constraint_;
            auto current = model_->animation_session_rotation_constraint(edit_id);
            if (!current) {
                clear_pending();
                report_failure(canv, sm::result::not_found, "Cannot edit transition rotation constraint");
                return;
            }
            current->reference = sm::rotation_reference::bone(target.id());
            const auto status = model_->update_animation_rotation_constraint(edit_id, *current);
            clear_pending();
            if (status != sm::result::success)
                report_failure(canv, status, "Cannot change transition rotation reference");
            else
                canv.select_constraint(edit_id);
            canv.sync_to_model();
            return;
        }
        auto result = animation
            ? model_->add_animation_rotation_constraint(target_id, sm::rotation_reference::bone(target.id()),
                {k_default_rot_constraint_min, k_default_rot_constraint_span})
            : model_->add_rotation_constraint(target_id, sm::rotation_reference::bone(target.id()),
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
    auto result = animation
        ? model_->add_animation_rotation_constraint(target.id(), reference,
            {k_default_rot_constraint_min, k_default_rot_constraint_span})
        : model_->add_rotation_constraint(target.id(), reference,
            {k_default_rot_constraint_min, k_default_rot_constraint_span});
    if (!result) {
        report_failure(canv, result.error(), "Cannot create rotation constraint");
        return;
    }
    if (canv.is_status_line_visible())
        canv.hide_status_line();
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
    if (!result)
        return;
    clear_pending();
    canv.select_constraint(*result);
}

void ui::tool::constraint::keyPressEvent(canvas::scene& canv, QKeyEvent* event) {
    if (event->key() == Qt::Key_Delete && selected_path_ && model_ && model_->animation_has_outgoing_transition()) {
        model_->set_animation_path(*selected_path_, std::nullopt);
        selected_path_.reset();
        redraw_paths(canv);
        return;
    }
    if (event->key() == Qt::Key_Escape && path_gesture_) {
        path_gesture_.reset();
        redraw_paths(canv);
        return;
    }

    if (event->key() == Qt::Key_Escape) {
        if (drag_)
            cancel_drag(canv);
        clear_triangle_sweep();
        clear_pending();
        canv.set_hovered_constraint({});
    }
}

void ui::tool::constraint::mousePressEvent(canvas::scene& canv, QGraphicsSceneMouseEvent* event) {
    press_handled_ = false;
    if (model_ && model_->animation_has_outgoing_transition()) {
        // Nodes retain pin/unpin priority in the pre-existing operations.
        auto hit = (canv.top_node(event->scenePos()) && current_operation() != operation::path)
            ? std::optional<std::pair<sm::object_id, int>>{}
            : hit_path(canv, event->scenePos());
        if (event->button() == Qt::RightButton && hit) {
            auto ctx = model_->animation_session_path_context(hit->first);
            if (!ctx)
                return;
            QMenu menu(settings_);
            QAction* insert = nullptr;
            QAction* remove = nullptr;
            if (hit->second == -1)
                insert = menu.addAction("Insert Knot");
            const auto k = static_cast<std::size_t>(std::max(0, hit->second) / 3);
            if (hit->second >= 0 && hit->second%3 == 0 && k > 0 && k + 1 < ctx->path.knots.size())
                remove = menu.addAction("Remove Knot");
            if (insert || remove) {
                const auto* action = menu.exec(event->screenPos());
                if (action == insert)
                    insert_path_knot(canv, hit->first, event->scenePos());
                if (action == remove) {
                    ctx->path.remove_knot(k);
                    if (model_->set_animation_path(hit->first, ctx->path) == sm::result::success)
                        select_path(canv, hit->first);
                }
                event->accept();
            }
            return;
        }
        if (event->button() == Qt::LeftButton && hit) {
            select_path(canv, hit->first);
            if (hit->second >= 0) {
                auto ctx = model_->animation_session_path_context(hit->first);
                if (ctx)
                    path_gesture_ = path_gesture{hit->first, ctx->path, ctx->path, hit->second, event->scenePos(), false};
            }
            press_handled_ = true;
            return;
        }
        if (event->button() == Qt::LeftButton && current_operation() == operation::path) {
            if (auto* node = canv.top_node(event->scenePos())) {
                auto id = node->model().id();
                auto ctx = model_->animation_session_path_context(id);
                if (ctx) {
                    auto path = ctx->path;
                    path.reset(ctx->start, ctx->end);
                    selected_path_ = id;
                    path_gesture_ = path_gesture{id, ctx->path, path, -1, event->scenePos(), true};
                    redraw_paths(canv);
                    press_handled_ = true;
                    return;
                }
            }
        }
    }
    if (event->button() != Qt::LeftButton)
        return;

    auto* item = canv.top_item(event->scenePos());

    // Node clicks always mean pin/unpin for the constraint tool.  Give nodes
    // priority over any constraint adornment that happens to overlap them.
    if (dynamic_cast<canvas::item::node*>(item))
        return;
    if (auto hit = canv.constraint_at(event->scenePos())) {
        if (model_ && model_->animation_mode() && !hit->transition_local)
            return;
        clear_triangle_sweep();
        clear_pending();
        canv.select_constraint(hit->id);
        if (model_ && model_->animation_mode() && hit->transition_local) {
            if (auto current = model_->animation_session_rotation_constraint(hit->id)) {
                QSignalBlocker blocker(reference_);
                reference_->setCurrentIndex(int(current->reference.kind));
            }
        }
        press_handled_ = true;
        if (hit->part != canvas::constraint_part::body) {
            if (model_ && model_->animation_mode()) {
                if (auto current = model_->animation_session_rotation_constraint(hit->id))
                    drag_ = drag_state{hit->id, hit->part, sm::constraint_definition{*current}};
            } else {
                auto current = model_->core().constraint_by_id(hit->id);
                if (current)
                    drag_ = drag_state{ hit->id, hit->part, current->get().definition() };
            }
        }
        return;
    }

    if (current_operation() == operation::rigid_triangle && !item &&
            !(model_ && model_->animation_mode())) {
        begin_triangle_sweep(canv, event->scenePos());
        press_handled_ = true;
    }
}

void ui::tool::constraint::update_drag(canvas::scene& canv, QPointF point) {
    if (!drag_)
        return;
    sm::constraint_definition definition;
    if (model_ && model_->animation_mode()) {
        auto current = model_->animation_session_rotation_constraint(drag_->id);
        if (!current) {
            drag_.reset();
            return;
        }
        definition = *current;
    } else {
        auto current = model_->core().constraint_by_id(drag_->id);
        if (!current) {
            drag_.reset();
            return;
        }
        definition = current->get().definition();
    }

    if (auto* rotation = std::get_if<sm::rotation_constraint>(&definition)) {
        auto target = model_->topology().get<sm::bone>(rotation->target_bone);
        if (!target)
            return;
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
        if (drag_->part != canvas::constraint_part::triangle_angle)
            return;
        auto first = model_->topology().get<sm::bone>(triangle->first_bone);
        if (!first)
            return;
        const auto pivot = first->get().parent_node().world_pos();
        const double world_angle = sm::angle_from_u_to_v(pivot, ui::from_qt_pt(point));
        triangle->relative_angle = sm::normalize_angle(world_angle - first->get().world_rotation());
    }

    const auto result = model_ && model_->animation_mode()
        ? (std::get_if<sm::rotation_constraint>(&definition)
            ? model_->preview_animation_rotation_constraint(drag_->id, std::get<sm::rotation_constraint>(definition))
            : sm::result::invalid_constraint)
        : model_->core().update_constraint(drag_->id, definition);
    if (result == sm::result::success)
        canv.sync_to_model();
    else
        report_failure(canv, result, "Cannot preview constraint edit");
}

void ui::tool::constraint::cancel_drag(canvas::scene& canv) {
    if (!drag_)
        return;
    if (model_ && model_->animation_mode()) {
        if (auto* rotation = std::get_if<sm::rotation_constraint>(&drag_->original))
            model_->preview_animation_rotation_constraint(drag_->id, *rotation);
    } else {
        model_->core().update_constraint(drag_->id, drag_->original);
    }
    drag_.reset();
    canv.sync_to_model();
}

void ui::tool::constraint::finish_drag(canvas::scene& canv) {
    if (!drag_)
        return;
    const auto id = drag_->id;
    const auto before = drag_->original;
    sm::constraint_definition after;
    if (model_ && model_->animation_mode()) {
        auto current = model_->animation_session_rotation_constraint(id);
        if (!current) {
            drag_.reset();
            return;
        }
        after = *current;
        auto* before_rotation = std::get_if<sm::rotation_constraint>(&before);
        auto* after_rotation = std::get_if<sm::rotation_constraint>(&after);
        if (!before_rotation || !after_rotation) {
            drag_.reset();
            return;
        }
        model_->preview_animation_rotation_constraint(id, *before_rotation);
        drag_.reset();
        const auto result = model_->update_animation_rotation_constraint(id, *after_rotation);
        if (result != sm::result::success) {
            model_->preview_animation_rotation_constraint(id, *before_rotation);
            report_failure(canv, result, "Cannot edit transition rotation constraint");
        }
        canv.select_constraint(id);
        canv.sync_to_model();
        return;
    }
    auto current = model_->core().constraint_by_id(id);
    if (!current) {
        drag_.reset();
        return;
    }
    after = current->get().definition();
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
    if (path_gesture_) {
        move_path_gesture(canv, event->scenePos());
        return;
    }
    if (drag_) {
        update_drag(canv, event->scenePos());
        return;
    }
    if (triangle_sweep_) {
        update_triangle_sweep(canv, event->scenePos());
        return;
    }
    auto hit = canv.constraint_at(event->scenePos());
    if (model_ && model_->animation_mode() && hit && !hit->transition_local)
        hit.reset();
    canv.set_hovered_constraint(hit ? std::optional<sm::object_id>{hit->id} : std::nullopt);

    if (pending_bone_) {
        if (auto* item = canv.top_item(event->scenePos());
            auto* bone = dynamic_cast<canvas::item::bone*>(item)) {
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
    if (path_gesture_ && event->button() == Qt::LeftButton) {
        move_path_gesture(canv, event->scenePos());
        finish_path_gesture(canv);
        press_handled_ = false;
        return;
    }
    if (event->button() != Qt::LeftButton)
        return;
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
        if (current_operation() == operation::select)
            canv.clear_constraint_selection();
        return;
    }
    if (auto* node = dynamic_cast<canvas::item::node*>(item)) {
        canv.toggle_node_pinned_undoable(node->model().id());
        return;
    }
    auto* bone = dynamic_cast<canvas::item::bone*>(item);
    if (!bone)
        return;

    switch (current_operation()) {
    case operation::select:
        return;
    case operation::rotation:
        create_rotation(canv, bone->model());
        return;
    case operation::path:
        return;
    case operation::rigid_triangle:
        // Transition-local rigid triangles are not implemented yet.  Keep the
        // operation selectable in Animation Mode, but make creation a no-op.
        if (model_ && model_->animation_mode())
            return;
        create_triangle(canv, bone->model());
        return;
    }
}

QWidget* ui::tool::constraint::settings_widget() { return settings_; }

namespace {
QPointF path_world(const mdl::project::animation_path_context& ctx, sm::point local) {
    return ui::to_qt_pt(ctx.frame.to_world(local));
}
sm::point knot_local(const mdl::project::animation_path_context& ctx,
    const sm::animation_path& path, std::size_t i) {
    if (i == 0)
        return ctx.start;
    if (i + 1 == path.knots.size())
        return ctx.end;
    return path.knots[i].position;
}
QPointF control_world(const mdl::project::animation_path_context& ctx,
    const sm::animation_path& path, int control) {
    const auto i = static_cast<std::size_t>(control / 3);
    const auto part = control%3;
    const auto origin = knot_local(ctx, path, i);
    if (part == 1)
        return path_world(ctx, origin + path.knots[i].handle_in);
    if (part == 2)
        return path_world(ctx, origin + path.knots[i].handle_out);
    return path_world(ctx, origin);
}
}

void ui::tool::constraint::clear_path_graphics() {
    if (path_scene_) {
        for (auto* g : path_graphics_) {
            path_scene_->removeItem(g);
            delete g;
        }
    }
    path_graphics_.clear();
    path_scene_.clear();
}

void ui::tool::constraint::redraw_paths(canvas::scene& canv) {
    clear_path_graphics();
    if (!active_ || !model_ || !model_->animation_has_outgoing_transition())
        return;
    path_scene_ = &canv;
    auto draw = [&](QGraphicsItem* item) {
        item->setZValue(3500);
        canv.addItem(item);
        path_graphics_.push_back(item);
    };
    auto draw_circle = [&](QPointF p, QColor color, double radius, bool ghost = false) {
        const double r = radius / std::max(0.01, canv.scale());
        auto* circle = new QGraphicsEllipseItem(p.x() - r, p.y() - r, 2 * r, 2 * r);
        circle->setPen(QPen(color, 1.5));
        circle->setBrush(ghost ? QBrush(Qt::NoBrush) : QBrush(color));
        circle->setZValue(3501);
        canv.addItem(circle);
        path_graphics_.push_back(circle);
    };
    auto ids = model_->animation_session_path_nodes();
    if (path_gesture_ && std::ranges::find(ids, path_gesture_->node) == ids.end())
        ids.push_back(path_gesture_->node);
    for (const auto id : ids) {
        auto ctx = model_->animation_session_path_context(id);
        if (!ctx)
            continue;
        auto path = ctx->path;
        if (path_gesture_ && path_gesture_->node == id)
            path = path_gesture_->edited;
        const bool selected = selected_path_ && *selected_path_ == id;
        QPainterPath curve(path_world(*ctx, ctx->start));
        const int samples = std::max(120, static_cast<int>(path.knots.size() > 1 ? path.knots.size() - 1 : 1) * 96);
        for (int i = 1; i <= samples; ++i)
            curve.lineTo(path_world(*ctx, path.at_parameter(double(i) / samples, ctx->start, ctx->end)));
        auto* line = new QGraphicsPathItem(curve);
        QPen pen(selected ? QColor(0, 160, 235) : QColor(70, 170, 205), 2, Qt::DashLine);
        pen.setCosmetic(true);
        line->setPen(pen);
        draw(line);
        if (!selected)
            continue;
        // The destination is always pose-owned; the hollow ghost is not a handle.
        draw_circle(path_world(*ctx, ctx->end), QColor(0, 170, 240), 7, true);
        draw_circle(path_world(*ctx, ctx->start), QColor(0, 170, 240), 4, true);
        if (path.knots.size() < 2)
            continue;
        for (std::size_t k = 0; k < path.knots.size(); ++k) {
            const auto origin = knot_local(*ctx, path, k);
            if (k > 0 && k + 1 < path.knots.size())
                draw_circle(path_world(*ctx, origin), QColor(255, 180, 30), 5);
            for (int side = 1; side <= 2; ++side) {
                if ((side == 1 && k == 0) || (side == 2 && k + 1 == path.knots.size()))
                    continue;
                const auto handle = control_world(*ctx, path, static_cast<int>(k * 3 + side));
                auto* guide = new QGraphicsLineItem(QLineF(path_world(*ctx, origin), handle));
                QPen thin(QColor(90, 150, 210), 1, Qt::DotLine);
                thin.setCosmetic(true);
                guide->setPen(thin);
                draw(guide);
                draw_circle(handle, QColor(50, 135, 235), 4);
            }
        }
    }
}

std::optional<std::pair<sm::object_id, int>> ui::tool::constraint::hit_path(
    canvas::scene& canv, QPointF where) const {
    if (!model_ || !model_->animation_has_outgoing_transition())
        return {};
    const double handle_radius = 9.0 / std::max(0.01, canv.scale());
    if (selected_path_) {
        auto ctx = model_->animation_session_path_context(*selected_path_);
        if (ctx && ctx->path.knots.size() >= 2) {
            const auto& path = ctx->path;
            for (std::size_t i = 0; i < path.knots.size(); ++i) {
                for (int side = 0; side < 3; ++side) {
                    if (side == 0 && (i == 0 || i + 1 == path.knots.size()))
                        continue;
                    if (side == 1 && i == 0)
                        continue;
                    if (side == 2 && i + 1 == path.knots.size())
                        continue;
                    const int control = static_cast<int>(3 * i + side);
                    if (QLineF(control_world(*ctx, path, control), where).length() <= handle_radius)
                        return std::pair{*selected_path_, control};
                }
            }
        }
    }
    const double path_radius = 7.0 / std::max(0.01, canv.scale());
    for (const auto id : model_->animation_session_path_nodes()) {
        auto ctx = model_->animation_session_path_context(id);
        if (!ctx)
            continue;
        auto previous = path_world(*ctx, ctx->start);
        const int samples = std::max(120, static_cast<int>(ctx->path.knots.size() > 1 ? ctx->path.knots.size() - 1 : 1) * 96);
        for (int i = 1; i <= samples; ++i) {
            const auto next = path_world(*ctx, ctx->path.at_parameter(double(i) / samples, ctx->start, ctx->end));
            QLineF section(previous, next);
            const auto v = next - previous;
    const auto w = where - previous;
            const double square = QPointF::dotProduct(v, v);
            const double u = square > 0 ? std::clamp(QPointF::dotProduct(v, w) / square, 0.0, 1.0) : 0;
            if (QLineF(previous + u * v, where).length() <= path_radius)
                return std::pair{id, -1};
            previous = next;
        }
    }
    return {};
}

void ui::tool::constraint::select_path(canvas::scene& canv, sm::object_id node) {
    selected_path_ = node;
    canv.clear_constraint_selection();
    update_settings_state();
    redraw_paths(canv);
}

void ui::tool::constraint::move_path_gesture(canvas::scene& canv, QPointF world) {
    if (!path_gesture_)
        return;
    auto ctx = model_->animation_session_path_context(path_gesture_->node);
    if (!ctx)
        return;
    auto& g = *path_gesture_;
    if (g.creating) {
        const auto start = ctx->frame.to_local(ui::from_qt_pt(g.press_point));
        const auto current = ctx->frame.to_local(ui::from_qt_pt(world));
        const auto bend = current - start;
        g.edited.reset(ctx->start, ctx->end);
        // Preserve the existing drag-out-a-curve interaction: the drag
        // offsets both Bezier handles relative to the initial curve.
        g.edited.knots.front().handle_out += bend;
        g.edited.knots.back().handle_in += bend;
    } else if (g.control >= 0) {
        const auto i = static_cast<std::size_t>(g.control / 3);
        const auto part = g.control % 3;
        if (i >= g.edited.knots.size())
            return;
        const auto p = ctx->frame.to_local(ui::from_qt_pt(world));
        auto& knot = g.edited.knots[i];
        if (part == 0 && i > 0 && i + 1 < g.edited.knots.size())
            knot.position = p;
        if (part == 1 || part == 2)
            g.edited.set_handle(i, part == 1, p - knot_local(*ctx, g.edited, i));
    }
    g.edited.invalidate();
    redraw_paths(canv);
}

void ui::tool::constraint::finish_path_gesture(canvas::scene& canv) {
    if (!path_gesture_)
        return;
    auto gesture = std::move(*path_gesture_);
    path_gesture_.reset();
    const auto result = model_->set_animation_path(gesture.node, std::move(gesture.edited));
    if (result != sm::result::success)
        report_failure(canv, result, "Cannot edit transition path");
    select_path(canv, gesture.node);
}

void ui::tool::constraint::insert_path_knot(canvas::scene& canv, sm::object_id id, QPointF click) {
    auto ctx = model_->animation_session_path_context(id);
    if (!ctx || ctx->path.knots.size() < 2)
        return;
    const auto count = ctx->path.knots.size() - 1;
    std::size_t best_segment = 0;
    double best_t = 0.5;
    double best_distance = std::numeric_limits<double>::infinity();
    auto distance_squared = [&](std::size_t segment, double t) {
        const auto u = (segment + t) / count;
        const auto d = path_world(*ctx, ctx->path.at_parameter(u, ctx->start, ctx->end)) - click;
        return QPointF::dotProduct(d, d);
    };
    // Find the nearest point, not merely the nearest segment. Refine the best
    // sample with golden-section search to place the inserted knot accurately.
    constexpr int samples = 64;
    for (std::size_t i = 0; i < count; ++i) {
        for (int k = 0; k <= samples; ++k) {
            const double t = double(k) / samples;
            const auto distance = distance_squared(i, t);
            if (distance < best_distance) {
                best_distance = distance;
                best_segment = i;
                best_t = t;
            }
        }
    }
    double left = std::max(0.0, best_t - 1.0 / samples);
    double right = std::min(1.0, best_t + 1.0 / samples);
    constexpr double ratio = 0.6180339887498948482;
    double x = right - ratio * (right - left);
    double y = left + ratio * (right - left);
    double fx = distance_squared(best_segment, x);
    double fy = distance_squared(best_segment, y);
    for (int i = 0; i < 28; ++i) {
        if (fx < fy) {
            right = y;
            y = x;
            fy = fx;
            x = right - ratio * (right - left);
            fx = distance_squared(best_segment, x);
        } else {
            left = x;
            x = y;
            fx = fy;
            y = left + ratio * (right - left);
            fy = distance_squared(best_segment, y);
        }
    }
    const double t = std::clamp((left + right) / 2, 1e-3, 1.0 - 1e-3);
    ctx->path.insert_knot(best_segment, t, ctx->start, ctx->end);
    const auto result = model_->set_animation_path(id, ctx->path);
    if (result == sm::result::success)
        select_path(canv, id);
    else
        report_failure(canv, result, "Cannot insert path knot");
}

void ui::tool::constraint::mouseDoubleClickEvent(canvas::scene& canv, QGraphicsSceneMouseEvent* event) {
    if (event->button() != Qt::LeftButton || !model_ || !model_->animation_has_outgoing_transition())
        return;
    auto hit = hit_path(canv, event->scenePos());
    if (!hit || hit->second != -1)
        return;
    path_gesture_.reset();
    insert_path_knot(canv, hit->first, event->scenePos());
}
