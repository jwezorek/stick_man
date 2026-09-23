#include "constraint_adornment.hpp"
#include "scene.hpp"
#include "canvas_item.hpp"
#include "../util.hpp"
#include <QGraphicsPathItem>
#include <QGraphicsEllipseItem>
#include <QPainterPathStroker>
#include <cmath>
#include <numbers>
#include <utility>

namespace {

constexpr double k_rotation_radius_px = 52.0;
constexpr double k_triangle_radius_px = 38.0;
constexpr double k_handle_radius_px = 5.5;
constexpr double k_hit_width_px = 12.0;
constexpr double k_z = 40.0;

QColor normal_color() { return QColor("mediumpurple"); }
QColor hover_color() { return QColor("orange"); }
QColor selected_color() { return ui::canvas::k_sel_color; }

class constraint_graphic {
public:
    constraint_graphic(sm::object_id id, ui::canvas::constraint_part part) : id_(id), part_(part) {}
    virtual ~constraint_graphic() = default;
    sm::object_id constraint_id() const { return id_; }
    ui::canvas::constraint_part part() const { return part_; }
    virtual void set_constraint_state(bool selected, bool hovered) = 0;
private:
    sm::object_id id_;
    ui::canvas::constraint_part part_;
};

class path_graphic final : public QGraphicsPathItem, public constraint_graphic {
public:
    path_graphic(sm::object_id id, ui::canvas::constraint_part part, double scale)
        : constraint_graphic(id, part), hit_width_(k_hit_width_px / scale), scale_(scale) {
        setZValue(k_z);
        setBrush(Qt::NoBrush);
    }
    QPainterPath shape() const override {
        QPainterPathStroker stroker;
        stroker.setWidth(hit_width_);
        stroker.setCapStyle(Qt::RoundCap);
        stroker.setJoinStyle(Qt::RoundJoin);
        return stroker.createStroke(path());
    }
    void set_constraint_state(bool selected, bool hovered) override {
        const QColor color = selected ? selected_color() : hovered ? hover_color() : normal_color();
        const double width = (selected ? 3.5 : hovered ? 3.0 : 2.0) / scale_;
        setPen(QPen(color, width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    }
private:
    double hit_width_;
    double scale_;
};

class handle_graphic final : public QGraphicsEllipseItem, public constraint_graphic {
public:
    handle_graphic(sm::object_id id, ui::canvas::constraint_part part, QPointF center, double scale)
        : constraint_graphic(id, part), scale_(scale) {
        const double r = k_handle_radius_px / scale;
        setRect(QRectF(center - QPointF(r, r), QSizeF(2 * r, 2 * r)));
        setZValue(k_z + 1.0);
    }
    void set_constraint_state(bool selected, bool hovered) override {
        const QColor color = selected ? selected_color() : hovered ? hover_color() : normal_color();
        setBrush(color);
        setPen(QPen(Qt::black, 1.0 / scale_));
    }
private:
    double scale_;
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

double rotation_reference_angle(const sm::topology& topology, const sm::rotation_constraint& constraint) {
    auto target = topology.get<sm::bone>(constraint.target_bone);
    if (!target) return 0.0;
    switch (constraint.reference.kind) {
    case sm::rotation_reference_kind::world:
        return 0.0;
    case sm::rotation_reference_kind::parent:
        if (auto parent = target->get().parent_bone()) return parent->get().world_rotation();
        return 0.0;
    case sm::rotation_reference_kind::bone:
        if (auto reference = topology.get<sm::bone>(constraint.reference.bone_id))
            return reference->get().world_rotation();
        return 0.0;
    }
    return 0.0;
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

void ui::canvas::constraint_adornment_layer::sync(const sm::project& project, double scale) {
    clear();
    const auto& topology = project.topology();
    for (const auto& [id, constraint] : project.constraints()) {
        visual v;
        if (auto rotation = constraint.rotation()) {
            auto target = topology.get<sm::bone>(rotation->target_bone);
            if (!target) continue;
            const QPointF pivot = ui::to_qt_pt(target->get().parent_node().world_pos());
            const double radius = k_rotation_radius_px / scale;
            const double start = rotation_reference_angle(topology, *rotation) + rotation->allowed.start_angle;
            const double end = start + rotation->allowed.span_angle;

            auto* body = new path_graphic(id, constraint_part::body, scale);
            body->setPath(arc_path(pivot, radius, start, rotation->allowed.span_angle));
            owner_.addItem(body); v.graphics.push_back(body);

            auto* min_handle = new handle_graphic(id, constraint_part::rotation_min,
                radial(pivot, radius, start), scale);
            auto* max_handle = new handle_graphic(id, constraint_part::rotation_max,
                radial(pivot, radius, end), scale);
            owner_.addItem(min_handle); owner_.addItem(max_handle);
            v.graphics.push_back(min_handle); v.graphics.push_back(max_handle);
        } else if (auto triangle = constraint.triangle()) {
            auto first = topology.get<sm::bone>(triangle->first_bone);
            auto second = topology.get<sm::bone>(triangle->second_bone);
            if (!first || !second) continue;
            const QPointF pivot = ui::to_qt_pt(first->get().parent_node().world_pos());
            const double radius = k_triangle_radius_px / scale;
            const double start = first->get().world_rotation();
            const double span = triangle->relative_angle;

            auto* body = new path_graphic(id, constraint_part::body, scale);
            body->setPath(arc_path(pivot, radius, start, span));
            owner_.addItem(body); v.graphics.push_back(body);

            // Put the edit handle at the relationship's second ray.  Dragging the
            // handle can then map directly to the stored first->second relative
            // angle, rather than requiring a special "half arc" interpretation.
            const double end = start + span;
            auto* handle = new handle_graphic(id, constraint_part::triangle_angle,
                radial(pivot, radius, end), scale);
            owner_.addItem(handle); v.graphics.push_back(handle);
        }
        for (auto* graphic : v.graphics) graphic->setVisible(visible_);
        visuals_.emplace(id, std::move(v));
    }
    if (selected_ && !visuals_.contains(*selected_)) selected_.reset();
    if (hovered_ && !visuals_.contains(*hovered_)) hovered_.reset();
    update_styles();
}

void ui::canvas::constraint_adornment_layer::set_visible(bool visible) {
    visible_ = visible;
    for (auto& [id, visual] : visuals_)
        for (auto* graphic : visual.graphics) graphic->setVisible(visible);
}

void ui::canvas::constraint_adornment_layer::set_selected(std::optional<sm::object_id> id) {
    selected_ = id;
    update_styles();
}

void ui::canvas::constraint_adornment_layer::set_hovered(std::optional<sm::object_id> id) {
    if (hovered_ == id) return;
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
    if (!visible_) return {};
    for (auto* graphic : owner_.items(point, Qt::IntersectsItemShape, Qt::DescendingOrder,
            owner_.views().isEmpty() ? QTransform{} : owner_.views().first()->viewportTransform())) {
        if (auto* item = dynamic_cast<const constraint_graphic*>(graphic))
            return constraint_hit{item->constraint_id(), item->part()};
    }
    return {};
}
