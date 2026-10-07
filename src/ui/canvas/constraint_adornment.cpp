#include "constraint_adornment.hpp"
#include "scene.hpp"
#include "canvas_item.hpp"
#include "../util.hpp"
#include <QGraphicsPathItem>
#include <QGraphicsEllipseItem>
#include <QGraphicsPolygonItem>
#include <QPainterPathStroker>
#include <cmath>
#include <numbers>
#include <utility>

namespace {

constexpr double k_rotation_arc_radius_px = 52.0;
constexpr double k_rotation_wedge_radius_px = 60.0;
constexpr double k_handle_radius_px = 5.5;
constexpr double k_hit_width_px = 12.0;
constexpr double k_z = 40.0;
constexpr double k_triangle_z = 4.0; // Below bones (z=5) and nodes (z=10).

enum class adornment_role {
    persistent,
    outgoing_transition,
    incoming_lock
};

QColor normal_color(adornment_role role) {
    if (role == adornment_role::incoming_lock) return QColor(128, 128, 128);
    return role == adornment_role::outgoing_transition ? QColor("deepskyblue") : QColor("mediumpurple");
}
QColor hover_color(adornment_role role) {
    if (role == adornment_role::incoming_lock) return QColor(128, 128, 128);
    return role == adornment_role::outgoing_transition ? QColor("cyan") : QColor("orange");
}
QColor selected_color(adornment_role role) {
    if (role == adornment_role::incoming_lock) return QColor(128, 128, 128);
    return role == adornment_role::outgoing_transition ? QColor("cyan") : ui::canvas::k_sel_color;
}

class constraint_graphic {
public:
    constraint_graphic(sm::object_id id, ui::canvas::constraint_part part, adornment_role role,
        bool interactive = true)
        : id_(id), part_(part), role_(role), interactive_(interactive) {}
    virtual ~constraint_graphic() = default;
    sm::object_id constraint_id() const { return id_; }
    ui::canvas::constraint_part part() const { return part_; }
    bool transition_local() const { return role_ == adornment_role::outgoing_transition; }
    adornment_role role() const { return role_; }
    bool interactive() const { return interactive_; }
    virtual void set_constraint_state(bool selected, bool hovered) = 0;
private:
    sm::object_id id_;
    ui::canvas::constraint_part part_;
    adornment_role role_ = adornment_role::persistent;
    bool interactive_ = true;
};

class path_graphic final : public QGraphicsPathItem, public constraint_graphic {
public:
    path_graphic(sm::object_id id, ui::canvas::constraint_part part, double scale,
        bool filled = false, adornment_role role = adornment_role::persistent, bool interactive = true)
        : constraint_graphic(id, part, role, interactive), hit_width_(k_hit_width_px / scale),
          scale_(scale), filled_(filled) {
        setZValue(k_z);
        setBrush(Qt::NoBrush);
        if (!interactive)
            setAcceptedMouseButtons(Qt::NoButton);
    }
    QPainterPath shape() const override {
        QPainterPathStroker stroker;
        stroker.setWidth(hit_width_);
        stroker.setCapStyle(Qt::RoundCap);
        stroker.setJoinStyle(Qt::RoundJoin);
        auto result = stroker.createStroke(path());
        if (filled_)
            result = result.united(path());
        return result;
    }
    void set_constraint_state(bool selected, bool hovered) override {
        const QColor color = selected ? selected_color(role()) : hovered ? hover_color(role()) : normal_color(role());
        const double width = (selected ? 3.5 : hovered ? 3.0 : 2.0) / scale_;
        setPen(QPen(color, width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        if (filled_) {
            QColor fill = color;
            fill.setAlpha(selected ? 96 : hovered ? 80 : 64);
            setBrush(fill);
        } else {
            setBrush(Qt::NoBrush);
        }
    }
private:
    double hit_width_;
    double scale_;
    bool filled_;
};

class handle_graphic final : public QGraphicsEllipseItem, public constraint_graphic {
public:
    handle_graphic(sm::object_id id, ui::canvas::constraint_part part, QPointF center, double scale,
        adornment_role role = adornment_role::persistent)
        : constraint_graphic(id, part, role), scale_(scale) {
        const double r = k_handle_radius_px / scale;
        setRect(QRectF(center - QPointF(r, r), QSizeF(2 * r, 2 * r)));
        setZValue(k_z + 1.0);
    }
    void set_constraint_state(bool selected, bool hovered) override {
        const QColor color = selected ? selected_color(role()) : hovered ? hover_color(role()) : normal_color(role());
        setBrush(color);
        setPen(QPen(Qt::black, 1.0 / scale_));
    }
private:
    double scale_;
};

class triangle_graphic final : public QGraphicsPolygonItem, public constraint_graphic {
public:
    triangle_graphic(sm::object_id id, const QPolygonF& polygon, double scale)
        : constraint_graphic(id, ui::canvas::constraint_part::body, adornment_role::persistent), scale_(scale) {
        setPolygon(polygon);
        setZValue(k_triangle_z);
    }
    void set_constraint_state(bool selected, bool hovered) override {
        setBrush(QColor(128, 128, 128));
        const QColor outline = selected ? selected_color(adornment_role::persistent) :
            hovered ? hover_color(adornment_role::persistent) : QColor(90, 90, 90);
        const double width = (selected ? 2.5 : hovered ? 2.0 : 1.0) / scale_;
        setPen(QPen(outline, width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    }
private:
    double scale_;
};

// Preserve the existing direct-manipulation affordance for the triangle angle
// without adding a visible gizmo on top of the filled triangle.  The hit target
// sits underneath the second bound node, so the node remains the visible handle.
class triangle_angle_hit_graphic final : public QGraphicsEllipseItem, public constraint_graphic {
public:
    triangle_angle_hit_graphic(sm::object_id id, QPointF center, double scale)
        : constraint_graphic(id, ui::canvas::constraint_part::triangle_angle, adornment_role::persistent) {
        const double r = ui::canvas::k_node_radius / scale;
        setRect(QRectF(center - QPointF(r, r), QSizeF(2 * r, 2 * r)));
        setZValue(k_triangle_z + 0.5);
        setPen(Qt::NoPen);
        setBrush(Qt::NoBrush);
    }
    QPainterPath shape() const override {
        QPainterPath path;
        path.addEllipse(rect());
        return path;
    }
    void set_constraint_state(bool, bool) override {}
};

QPointF radial(QPointF center, double radius, double theta) {
    return center + QPointF(radius * std::cos(theta), radius * std::sin(theta));
}

QPainterPath arc_path(QPointF center, double radius, double start, double span) {
    QRectF rect(center - QPointF(radius, radius), QSizeF(2 * radius, 2 * radius));
    QPainterPath path;
    const double start_deg = -ui::radians_to_degrees(start);
    const double span_deg = -ui::radians_to_degrees(span);
    path.arcMoveTo(rect, start_deg);
    path.arcTo(rect, start_deg, span_deg);
    return path;
}

QPainterPath wedge_path(QPointF center, double radius, double start, double span) {
    QRectF rect(center - QPointF(radius, radius), QSizeF(2 * radius, 2 * radius));
    const double start_deg = -ui::radians_to_degrees(start);
    const double span_deg = -ui::radians_to_degrees(span);

    QPainterPath path;
    path.moveTo(center);
    path.lineTo(radial(center, radius, start));
    path.arcTo(rect, start_deg, span_deg);
    path.closeSubpath();
    return path;
}

QPointF bone_midpoint(const sm::bone& bone) {
    const auto a = bone.parent_node().world_pos();
    const auto b = bone.child_node().world_pos();
    return ui::to_qt_pt(sm::point{(a.x + b.x) / 2.0, (a.y + b.y) / 2.0});
}

double rotation_reference_angle(const sm::topology& topology, const sm::rotation_constraint& constraint) {
    auto target = topology.get<sm::bone>(constraint.target_bone);
    if (!target)
        return 0.0;
    switch (constraint.reference.kind) {
    case sm::rotation_reference_kind::world:
        return 0.0;
    case sm::rotation_reference_kind::parent:
        if (auto parent = target->get().parent_bone())
            return parent->get().world_rotation();
        return 0.0;
    case sm::rotation_reference_kind::bone:
        if (auto reference = topology.get<sm::bone>(constraint.reference.bone_id))
            return reference->get().world_rotation();
        return 0.0;
    }
    return 0.0;
}

QGraphicsPathItem* make_lock_badge(QPointF position) {
    auto* lock = new QGraphicsPathItem;
    QPainterPath path;
    path.addRoundedRect(QRectF(-4, -2, 8, 7), 1, 1);
    path.moveTo(-2, -2);
    path.arcTo(QRectF(-2, -7, 4, 8), 0, 180);
    lock->setPath(path);
    lock->setPos(position);
    lock->setBrush(Qt::white);
    lock->setPen(QPen(QColor(80, 80, 80), 1.4));
    lock->setZValue(k_z + 2.0);
    lock->setFlag(QGraphicsItem::ItemIgnoresTransformations, true);
    lock->setAcceptedMouseButtons(Qt::NoButton);
    lock->setToolTip(QStringLiteral("Rotation locked by previous transition."));
    return lock;
}

} // namespace

ui::canvas::constraint_adornment_layer::constraint_adornment_layer(scene& owner) : owner_(owner) {}
ui::canvas::constraint_adornment_layer::~constraint_adornment_layer() { clear(); }

void ui::canvas::constraint_adornment_layer::clear() {
    for (auto& [id, visual] : visuals_) {
        for (auto* graphic : visual.graphics) {
            owner_.removeItem(graphic);
            delete graphic;
        }
    }
    visuals_.clear();
}

void ui::canvas::constraint_adornment_layer::sync(
        const sm::topology& topology, const sm::constraint_map& constraints,
        const sm::constraint_map& transition_constraints,
        const sm::constraint_map& incoming_locked_constraints, double scale) {
    clear();
    auto add = [&](const sm::constraint_map& source, adornment_role role) {
    for (const auto& [id, constraint] : source) {
        visual v;
        v.transition_local = role == adornment_role::outgoing_transition;
        if (auto rotation = constraint.rotation()) {
            auto target = topology.get<sm::bone>(rotation->target_bone);
            if (!target) continue;
            const bool filled_wedge = rotation->reference.kind == sm::rotation_reference_kind::world ||
                rotation->reference.kind == sm::rotation_reference_kind::parent;
            const QPointF pivot = rotation->reference.kind == sm::rotation_reference_kind::world
                ? bone_midpoint(target->get()) : ui::to_qt_pt(target->get().parent_node().world_pos());
            const double radial_offset_px = role == adornment_role::outgoing_transition ? 8.0 :
                role == adornment_role::incoming_lock ? 16.0 : 0.0;
            const double radius = ((filled_wedge ? k_rotation_wedge_radius_px : k_rotation_arc_radius_px)
                + radial_offset_px) / scale;
            const double start = rotation_reference_angle(topology, *rotation) + rotation->allowed.start_angle;
            const double end = start + rotation->allowed.span_angle;
            const bool incoming_lock = role == adornment_role::incoming_lock;
            auto* body = new path_graphic(id, constraint_part::body, scale, filled_wedge, role, !incoming_lock);
            body->setPath(filled_wedge ? wedge_path(pivot, radius, start, rotation->allowed.span_angle)
                                      : arc_path(pivot, radius, start, rotation->allowed.span_angle));
            owner_.addItem(body); v.graphics.push_back(body);
            if (incoming_lock) {
                const double badge_angle = start + rotation->allowed.span_angle / 2.0;
                auto* badge = make_lock_badge(radial(pivot, radius + 12.0 / scale, badge_angle));
                owner_.addItem(badge);
                v.graphics.push_back(badge);
            } else {
                auto* min_handle = new handle_graphic(id, constraint_part::rotation_min,
                    radial(pivot, radius, start), scale, role);
                auto* max_handle = new handle_graphic(id, constraint_part::rotation_max,
                    radial(pivot, radius, end), scale, role);
                owner_.addItem(min_handle); owner_.addItem(max_handle);
                v.graphics.push_back(min_handle); v.graphics.push_back(max_handle);
            }
        } else if (role == adornment_role::persistent) {
            auto triangle = constraint.triangle();
            auto first = topology.get<sm::bone>(triangle->first_bone);
            auto second = topology.get<sm::bone>(triangle->second_bone);
            if (!first || !second) continue;
            const QPointF root = ui::to_qt_pt(first->get().parent_node().world_pos());
            const QPointF first_tip = ui::to_qt_pt(first->get().child_node().world_pos());
            const QPointF second_tip = ui::to_qt_pt(second->get().child_node().world_pos());
            auto* body = new triangle_graphic(id, QPolygonF{root, first_tip, second_tip}, scale);
            owner_.addItem(body); v.graphics.push_back(body);
            auto* handle = new triangle_angle_hit_graphic(id, second_tip, scale);
            owner_.addItem(handle); v.graphics.push_back(handle);
        }
        for (auto* graphic : v.graphics) {
            const auto* item = dynamic_cast<constraint_graphic*>(graphic);
            const bool rotation_handle = item && (item->part() == constraint_part::rotation_min || item->part() == constraint_part::rotation_max);
            graphic->setVisible(visible_ && (!rotation_handle || handles_visible_));
        }
        visuals_.emplace(id, std::move(v));
    }};
    add(constraints, adornment_role::persistent);
    add(transition_constraints, adornment_role::outgoing_transition);
    add(incoming_locked_constraints, adornment_role::incoming_lock);
    if (selected_ && !visuals_.contains(*selected_)) selected_.reset();
    if (hovered_ && !visuals_.contains(*hovered_)) hovered_.reset();
    update_styles();
}

void ui::canvas::constraint_adornment_layer::set_visible(bool visible) {
    visible_ = visible;
    for (auto& [id, visual] : visuals_) {
        for (auto* graphic : visual.graphics) {
            const auto* constraint_item = dynamic_cast<constraint_graphic*>(graphic);
            const bool rotation_handle = constraint_item &&
                (constraint_item->part() == constraint_part::rotation_min ||
                 constraint_item->part() == constraint_part::rotation_max);
            graphic->setVisible(visible && (!rotation_handle || handles_visible_));
        }
    }
}

void ui::canvas::constraint_adornment_layer::set_handles_visible(bool visible) {
    handles_visible_ = visible;
    for (auto& [id, visual] : visuals_) {
        for (auto* graphic : visual.graphics) {
            const auto* constraint_item = dynamic_cast<constraint_graphic*>(graphic);
            if (!constraint_item)
                continue;
            if (constraint_item->part() == constraint_part::rotation_min ||
                constraint_item->part() == constraint_part::rotation_max)
                graphic->setVisible(visible_ && visible);
        }
    }
}

void ui::canvas::constraint_adornment_layer::set_selected(std::optional<sm::object_id> id) {
    selected_ = id;
    update_styles();
}

void ui::canvas::constraint_adornment_layer::set_hovered(std::optional<sm::object_id> id) {
    if (hovered_ == id)
        return;
    hovered_ = id;
    update_styles();
}

void ui::canvas::constraint_adornment_layer::update_styles() {
    for (auto& [id, visual] : visuals_) {
        const bool selected = selected_ && *selected_ == id;
        const bool hovered = hovered_ && *hovered_ == id;
        for (auto* graphic : visual.graphics)
            if (auto* constraint_graphic_item = dynamic_cast<constraint_graphic*>(graphic))
                constraint_graphic_item->set_constraint_state(selected, hovered);
    }
}

std::optional<ui::canvas::constraint_hit> ui::canvas::constraint_adornment_layer::hit(const QPointF& point) const {
    if (!visible_)
        return {};
    for (auto* graphic : owner_.items(point, Qt::IntersectsItemShape, Qt::DescendingOrder,
            owner_.views().isEmpty() ? QTransform{} : owner_.views().first()->viewportTransform())) {
        if (auto* item = dynamic_cast<const constraint_graphic*>(graphic); item && item->interactive())
            return constraint_hit{item->constraint_id(), item->part(), item->transition_local()};
    }
    return {};
}
