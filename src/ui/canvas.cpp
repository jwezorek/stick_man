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

    double radians_to_degrees(double radians) {
        return radians * (180.0 / std::numbers::pi_v<double>);
    }

    QPointF to_qt_pt(const sm::point& pt) {
        return { pt.x, pt.y };
    }

    void draw_grid_lines(QPainter* painter, const QRectF& r, double line_spacing) {
        painter->fillRect(r, Qt::white);
        qreal dimmer = 0.6666;
        QPen blue_pen(QColor::fromCmykF(0.4*dimmer, 0, 0, 0.1 * dimmer), 0.0);
        QPen dark_blue_pen(QColor::fromCmykF(0.8 * dimmer, 0, 0, 0.1 * dimmer), 0.0);
        qreal x1, y1, x2, y2;
        r.getCoords(&x1, &y1, &x2, &y2);

        int left_gridline_index = static_cast<int>(std::ceil(x1 / line_spacing));
        int right_gridline_index = static_cast<int>(std::floor(x2 / line_spacing));
        for (auto i : rv::iota(left_gridline_index, right_gridline_index + 1)) {
            auto x = i * line_spacing;
            painter->setPen( (i % 5) ? blue_pen : dark_blue_pen  );
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

    QPolygonF bone_polygon(double length, double joint_radius) {
        auto r = static_cast<float>(joint_radius);
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
}

/*------------------------------------------------------------------------------------------------*/

QGraphicsRectItem* make_rect() {
    QGraphicsRectItem* ri = new QGraphicsRectItem(0, 0, 40, 20);
    ri->setTransformOriginPoint(20, 10);
    ri->setBrush(Qt::yellow);
    return ri;
}

QGraphicsPolygonItem* make_poly() {
    QGraphicsPolygonItem* ri = new QGraphicsPolygonItem(
        QPolygon(
            QList<QPoint>{
                {-20,-20},
                {-20,20},
                {20, 20},
                {20, -20},
                { -20,-20 }
            }
        )
    );
    ri->setTransformOriginPoint(0, 0);
    ri->setBrush(Qt::yellow);
    return ri;
}

ui::canvas::canvas(){
    setSceneRect(QRectF(-1500, -1500, 3000, 3000));

    QTransform transform(
        1, 0, 0,
        0, 1, 0,
        20, 10, 1
    );
    /*
    auto a = make_rect();
    auto b = make_rect();
    auto c = make_rect();
    b->setParentItem(a);
    b->setTransform(transform);
    c->setParentItem(b);
    c->setTransform(transform);
    this->addItem(a);
    b->setZValue(10);
    */

    /*
    auto a = make_rect();
    auto b = make_rect();
    auto c = make_rect();

    b->setParentItem(a);
    b->setPos({ 20,10 });

    c->setParentItem(b);
    c->setPos({ 20,10 });

    a->setPos(400, 0);
    this->addItem(a);
    */

    auto a = new QGraphicsEllipseItem(-10,-10,20,20);
    a->setPos(400, 0);
    this->addItem(a);

    auto b = make_poly();
    auto c = make_poly();

    b->setParentItem(a);
    b->setPos({ 20,20 });

    c->setParentItem(b);
    c->setPos({ 20,20 });

    
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
    setViewportUpdateMode(QGraphicsView::FullViewportUpdate);
    setScene(new ui::canvas()); 
    scale(1, -1);
    
    /*
    connect(view_, &QGraphicsView::rubberBandChanged,
        [](QRect rubberBandRect, QPointF fromScenePoint, QPointF toScenePoint) {
            qDebug() << "rubber band";
        }
    );
    */
}

ui::canvas& ui::canvas_view::canvas() {
    return *static_cast<ui::canvas*>(this->scene());
}

ui::stick_man& ui::canvas_view::main_window() {
    return *static_cast<stick_man*>( parentWidget() );
}

/*------------------------------------------------------------------------------------------------*/

ui::joint_item::joint_item(sm::joint& joint) :
    joint_(joint),
    QGraphicsEllipseItem(
        -k_joint_radius, 
        -k_joint_radius, 
        2.0 * k_joint_radius, 
        2.0 * k_joint_radius
    )
{
    setBrush(QBrush(Qt::white));
    setPen(QPen(Qt::black, 2));
    joint_.set_user_data(std::ref(*this));
    setPos(joint.world_x(), joint.world_y());
}


sm::joint& ui::joint_item::joint() const {

    return joint_;

}

/*------------------------------------------------------------------------------------------------*/

ui::bone_item::bone_item(sm::bone& bone) : bone_(bone) {
    auto length = bone.length();
    setPolygon(bone_polygon(length, k_joint_radius));
    setTransformOriginPoint({ 0,0 });
    setRotation(radians_to_degrees(bone.rotation()));

    auto& parent = parent_joint_item();
    setParentItem(&parent);
    
    auto& child = child_joint_item();
    
    child.setParentItem(this);
    child.setPos( length, 0  );
    
    bone_.set_user_data(std::ref(*this));
}

ui::joint_item&  ui::bone_item::parent_joint_item() const {
    auto& parent_joint = std::any_cast<std::reference_wrapper<ui::joint_item>>(
        bone_.parent_joint().get_user_data()
    ).get();
    return parent_joint;
}

ui::joint_item& ui::bone_item::child_joint_item() const {
    auto& child_joint = std::any_cast<std::reference_wrapper<ui::joint_item>>(
        bone_.child_joint().get_user_data()
    ).get();
    return child_joint;
}
