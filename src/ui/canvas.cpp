#include "canvas.h"
#include "stick_man.h"
#include "tools.h"
#include "../core/ik_sandbox.h"
#include <boost/geometry.hpp>
#include <ranges>
#include <numbers>

namespace r = std::ranges;
namespace rv = std::ranges::views;

/*------------------------------------------------------------------------------------------------*/

namespace {

    constexpr double k_joint_radius = 8.0;
    constexpr double k_pin_radius = k_joint_radius - 3.0;
    constexpr int k_joint_zorder = 10;
    constexpr int k_bone_zorder = 5;

    template<typename T>
    std::vector<T*> child_items_of_type(const QList<QGraphicsItem*>& items) {
        return items |
            rv::transform(
                [](auto itm)->T* {  return dynamic_cast<T*>(itm); }
            ) | rv::filter(
                [](T* ptr)->bool { return ptr;  }
        ) | r::to<std::vector<T*>>();
    }

    ui::abstract_stick_man_item* to_stick_man(QGraphicsItem* itm) {
        return dynamic_cast<ui::abstract_stick_man_item*>(itm);
    }

    double radians_to_degrees(double radians) {
        return radians * (180.0 / std::numbers::pi_v<double>);
    }

    QPointF to_qt_pt(const sm::point& pt) {
        return { pt.x, pt.y };
    }

    void draw_grid_lines(QPainter* painter, const QRectF& r, double line_spacing) {
        painter->fillRect(r, Qt::white);
        qreal dimmer = 0.6666;
        QPen blue_pen(QColor::fromCmykF(0.4 * dimmer, 0, 0, 0.1 * dimmer), 0.0);
        QPen dark_blue_pen(QColor::fromCmykF(0.8 * dimmer, 0, 0, 0.1 * dimmer), 0.0);
        qreal x1, y1, x2, y2;
        r.getCoords(&x1, &y1, &x2, &y2);

        int left_gridline_index = static_cast<int>(std::ceil(x1 / line_spacing));
        int right_gridline_index = static_cast<int>(std::floor(x2 / line_spacing));
        for (auto i : rv::iota(left_gridline_index, right_gridline_index + 1)) {
            auto x = i * line_spacing;
            painter->setPen((i % 5) ? blue_pen : dark_blue_pen);
            painter->drawLine(x, y1, x, y2);
        }

        int top_gridline_index = static_cast<int>(std::ceil(y1 / line_spacing));
        int bottom_gridline_index = static_cast<int>(std::floor(y2 / line_spacing));
        for (auto i : rv::iota(top_gridline_index, bottom_gridline_index + 1)) {
            auto y = i * line_spacing;
            painter->setPen((i % 5) ? blue_pen : dark_blue_pen);
            painter->drawLine(x1, y, x2, y);
        }
    }

    QPolygonF bone_polygon(double length, double joint_radius, double scale) {
        auto r = static_cast<float>(joint_radius * scale);
        auto d = static_cast<float>(length);
        auto x1 = (r * r) / d;
        auto y1 = r * std::sqrt(1.0f - (r * r) / (d * d));
        QList<QPointF> pts = {
            {0,0},
            {x1, y1 },
            {d, 0},
            {x1, -y1},
            {0,0}
        };
        return { pts };
    }

    auto child_bones(ui::joint_item* joint) {
        auto bones = joint->model().child_bones();
        return bones |
            rv::transform(
                [](sm::const_bone_ref bone)->ui::bone_item& {
                    return std::any_cast<std::reference_wrapper<ui::bone_item>>(
                        bone.get().get_user_data()
                    ).get();
                }
        );
    }

    void set_circle(QGraphicsEllipseItem* ei, QPointF pos, double radius, double scale) {
        ei->setPos(0, 0);
        double r = radius * scale;
        ei->setRect(
            QRectF(
                -r, -r, 2.0 * r, 2.0 * r
            )
        );
        ei->setPos(pos);
    }

    void set_bone_item_pos(ui::bone_item* itm, double len, const sm::point& pos, double rot, double scale) {
        itm->setPos(0, 0);
        itm->setRotation(0);
        itm->setPolygon(bone_polygon(len, k_joint_radius, scale));
        itm->setRotation(radians_to_degrees(rot));
        itm->setPos(to_qt_pt(pos));
    }

}
/*------------------------------------------------------------------------------------------------*/

ui::canvas::canvas(){
    setSceneRect(QRectF(-1500, -1500, 3000, 3000));
    
}

void ui::canvas::drawBackground(QPainter* painter, const QRectF& dirty_rect) {
    painter->fillRect(dirty_rect, QColor::fromRgb(53,53,53));
    painter->setRenderHint(QPainter::Antialiasing, true);
    auto scene_rect = sceneRect();
    auto rect = dirty_rect.intersected(scene_rect);

    draw_grid_lines(painter, rect, k_grid_line_spacing);
}

ui::tool_manager& ui::canvas::tool_mgr() {
    return view().main_window().tool_mgr();
}


void ui::canvas::set_scale(double scale, std::optional<QPointF> center) {
    auto& view = this->view();
    view.resetTransform();
    view.scale(scale, -scale);
    if (center) {
        view.centerOn(*center);
    }
    sync_to_model();
}

double ui::canvas::scale() {
    auto& view = this->view();
    return view.transform().m11();
}

void ui::canvas::sync_to_model() {
    auto is_non_null = [](auto* p) {return p; };
    for (auto* child : items() | rv::transform(to_stick_man) | rv::filter(is_non_null)) {
        child->sync_to_model();
    }
}

ui::canvas_view& ui::canvas::view() {
    return *static_cast<ui::canvas_view*>(this->views()[0]);
}

ui::joint_item* ui::canvas::top_joint(const QPointF& pt) const {
    auto itms = items(pt);
    auto joints_at_pt = itms |
        rv::transform(
            [](auto itm)->ui::joint_item* {return dynamic_cast<ui::joint_item*>(itm); }
        );
    auto first = r::find_if( joints_at_pt, [](auto ptr) { return ptr != nullptr; } );
    if (first == joints_at_pt.end()) {
        return nullptr;
    }
    return *first;
}

std::vector<ui::joint_item*> ui::canvas::root_joint_items() const {
    auto joints = joint_items();
    return joints |
        rv::filter(
            [](auto j)->bool {
                return !(j->parentItem());
            }
        ) | r::to< std::vector<ui::joint_item*>>();
}

std::vector<ui::joint_item*> ui::canvas::joint_items() const {
    return child_items_of_type<joint_item>(items());
}

std::vector<ui::bone_item*> ui::canvas::bone_items() const {
    return child_items_of_type<bone_item>(items());
}

void ui::canvas::keyPressEvent(QKeyEvent* event) {
    tool_mgr().keyPressEvent(*this, event);
}

void ui::canvas::keyReleaseEvent(QKeyEvent* event) {
    tool_mgr().keyReleaseEvent(*this, event);
}

void ui::canvas::mousePressEvent(QGraphicsSceneMouseEvent* event) {
    tool_mgr().mousePressEvent(*this, event);
}

void ui::canvas::mouseMoveEvent(QGraphicsSceneMouseEvent* event) {
    tool_mgr().mouseMoveEvent(*this, event);
}

void ui::canvas::mouseReleaseEvent(QGraphicsSceneMouseEvent* event) {
    tool_mgr().mouseReleaseEvent(*this, event);
}

void ui::canvas::mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event) {
    tool_mgr().mouseDoubleClickEvent(*this, event);
}

void ui::canvas::wheelEvent(QGraphicsSceneWheelEvent* event) {
    tool_mgr().wheelEvent(*this, event);
}

/*------------------------------------------------------------------------------------------------*/

ui::canvas_view::canvas_view() {
    setRenderHint(QPainter::Antialiasing, true);
    setViewportUpdateMode(QGraphicsView::FullViewportUpdate);
    setScene(new ui::canvas()); 
    scale(1, -1);
}

ui::canvas& ui::canvas_view::canvas() {
    return *static_cast<ui::canvas*>(this->scene());
}

ui::stick_man& ui::canvas_view::main_window() {
    return *static_cast<stick_man*>( parentWidget() );
}

/*------------------------------------------------------------------------------------------------*/

ui::canvas* ui::abstract_stick_man_item::canvas() {
    auto* ptr = dynamic_cast<QGraphicsItem*>(this);
    return static_cast<ui::canvas*>(ptr->scene());
}

/*------------------------------------------------------------------------------------------------*/

ui::joint_item::joint_item(sm::joint& joint, double scale) :
        has_stick_man_model<ui::joint_item, sm::joint&>(joint),
        pin_(nullptr) {
    auto inv_scale = 1.0 / scale;
    setBrush(Qt::white);
    setPen(QPen(Qt::black, 2.0 * inv_scale));
    set_circle(this, to_qt_pt(model_.world_pos()), k_joint_radius, inv_scale);
    setZValue(k_joint_zorder);
}

void ui::joint_item::set_pinned(bool pinned) {
    if (!pin_) {
        pin_ = new QGraphicsEllipseItem();
        set_circle(pin_, { 0,0 }, k_pin_radius, 1.0 / canvas()->scale());
        pin_->setPen(Qt::NoPen);
        pin_->setBrush(Qt::black);
        pin_->setParentItem(this);
    }
    if (pinned) {
        pin_->show();
    } else {
        pin_->hide();
    }
    model_.set_pinned(pinned);
}

void ui::joint_item::sync_to_model() {
    auto& canv = *canvas();
    double inv_scale = 1.0 / canv.scale();
    setPen(QPen(Qt::black, 2.0 * inv_scale));
    set_circle(this, to_qt_pt(model_.world_pos()), k_joint_radius, inv_scale);
    if (pin_) {
        set_circle(pin_, { 0,0 }, k_pin_radius, inv_scale);
    }
}

/*------------------------------------------------------------------------------------------------*/

ui::bone_item::bone_item(sm::bone& bone, double scale) : 
        has_stick_man_model<ui::bone_item, sm::bone&>(bone) {
    setBrush(Qt::black);
    setPen(QPen(Qt::black, 1.0/scale));
    set_bone_item_pos(
        this,
        bone.scaled_length(),
        bone.parent_joint().world_pos(),
        bone.world_rotation(),
        1.0 / scale
    );
    setZValue(k_bone_zorder);
}

ui::joint_item&  ui::bone_item::parent_joint_item() const {
    auto& parent_joint = std::any_cast<std::reference_wrapper<ui::joint_item>>(
        model_.parent_joint().get_user_data()
    ).get();
    return parent_joint;
}

ui::joint_item& ui::bone_item::child_joint_item() const {
    auto& child_joint = std::any_cast<std::reference_wrapper<ui::joint_item>>(
        model_.child_joint().get_user_data()
    ).get();
    return child_joint;
}

void ui::bone_item::sync_to_model() {
    auto& canv = *canvas();
    setPen(QPen(Qt::black, 1.0 / canv.scale()));
    set_bone_item_pos(
        this,
        model_.scaled_length(),
        model_.parent_joint().world_pos(),
        model_.world_rotation(),
        1.0 / canv.scale()
    );
}