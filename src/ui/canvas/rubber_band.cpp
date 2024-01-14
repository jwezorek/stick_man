#include "rubber_band.h"
#include "../util.h"

/*------------------------------------------------------------------------------------------------*/

void ui::canvas::item::rubber_band::set_pinned_point(const QPointF& pt) {
    pinned_point_ = pt;
}

void ui::canvas::item::rubber_band::hide() {
    auto* ptr = dynamic_cast<QGraphicsItem*>(this);
    ptr->hide();
    qDebug() << "hide: " << ptr->isVisible();
}

/*------------------------------------------------------------------------------------------------*/

ui::canvas::item::rect_rubber_band::rect_rubber_band(const QPointF& pt) {
    setPen(QPen(Qt::black, 2, Qt::DotLine));
    setBrush(Qt::NoBrush);
    set_pinned_point(pt);
}

void ui::canvas::item::rect_rubber_band::handle_drag(const QPointF& pt) {
    QRectF rect(pinned_point_, pt);
    setRect(rect);
}

/*------------------------------------------------------------------------------------------------*/

ui::canvas::item::line_rubber_band::line_rubber_band(const QPointF& pt) {
    setPen(QPen(Qt::black, 2, Qt::DotLine));
    set_pinned_point(pt);
}

void ui::canvas::item::line_rubber_band::handle_drag(const QPointF& pt) {
    QLineF line(pinned_point_, pt);
    setLine(line);
}

/*------------------------------------------------------------------------------------------------*/

ui::canvas::item::arc_rubber_band::arc_rubber_band(const QPointF& pt) {
    setPen(QPen(Qt::black, 2, Qt::DotLine));
    setBrush(Qt::NoBrush);
    set_pinned_point(pt);
}

void ui::canvas::item::arc_rubber_band::handle_drag(const QPointF& pt) {
    auto* this_ellipse_item = dynamic_cast<QGraphicsEllipseItem*>(this);
    auto radius = distance(pinned_point_, pt);
    auto theta = angle_through_points(pinned_point_, pt);
    set_arc(this_ellipse_item, pinned_point_, radius, 0.0, theta);
}
