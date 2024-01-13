#include "rubber_band.h"

/*------------------------------------------------------------------------------------------------*/

void ui::canvas::item::rubber_band::set_pinned_point(const QPointF& pt) {
    pinned_point_ = pt;
}

/*------------------------------------------------------------------------------------------------*/

ui::canvas::item::rect_rubber_band::rect_rubber_band(const QPointF& pt) {
    setPen(QPen(Qt::black, 3, Qt::DotLine));
    setBrush(Qt::NoBrush);
    setVisible(false);
    set_pinned_point(pt);
}

void ui::canvas::item::rect_rubber_band::handle_drag(const QPointF& pt) {
    QRectF rect(pinned_point_, pt);
    setRect(rect);
}