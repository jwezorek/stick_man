#include "canvas.h"
#include "stick_man.h"
#include "tools.h"
#include <boost/geometry.hpp>
#include <ranges>

namespace r = std::ranges;
namespace rv = std::ranges::views;

/*------------------------------------------------------------------------------------------------*/

namespace {

    void draw_grid_lines(QPainter* painter, const QRectF& r, double line_spacing) {
        qreal dimmer = 0.6666;
        QPen blue_pen(QColor::fromCmykF(0.4*dimmer, 0, 0, 0.1 * dimmer), 0.0);
        QPen dark_blue_pen(QColor::fromCmykF(0.8 * dimmer, 0, 0, 0.1 * dimmer), 0.0);
        qreal x1, y1, x2, y2;
        r.getCoords(&x1, &y1, &x2, &y2);

        int left_gridline_index = static_cast<int>(std::ceil(x1 / line_spacing));
        int right_gridline_index = static_cast<int>(std::floor(x2 / line_spacing));
        for (auto i : rv::iota(left_gridline_index, right_gridline_index + 1)) {
            int x = static_cast<int>(i * line_spacing);
            painter->setPen( (i % 5) ? blue_pen : dark_blue_pen  );
            painter->drawLine(x, y1, x, y2);
        }

        int top_gridline_index = static_cast<int>(std::ceil(y1 / line_spacing));
        int bottom_gridline_index = static_cast<int>(std::floor(y2 / line_spacing));
        for (auto i : rv::iota(top_gridline_index, bottom_gridline_index + 1)) {
            int y = static_cast<int>(i * line_spacing);
            painter->setPen((i % 5) ? blue_pen : dark_blue_pen);
            painter->drawLine(x1, y, x2, y);
        }

    }

    void add_test_poly(ui::canvas* c) {
        auto poly = new QGraphicsPolygonItem();
        QPainterPath path;
        path.moveTo(200, 50);
        path.arcTo(150, 0, 50, 50, 0, 90);
        path.arcTo(50, 0, 50, 50, 90, 90);
        path.arcTo(50, 50, 50, 50, 180, 90);
        path.arcTo(150, 50, 50, 50, 270, 90);
        path.lineTo(200, 25);
        auto myPolygon = path.toFillPolygon();
        poly->setPolygon(myPolygon);
        c->addItem(poly);
    }
}

/*------------------------------------------------------------------------------------------------*/

ui::canvas::canvas(){
    setSceneRect(QRectF(-1000, -1000, 2000, 2000));
    add_test_poly(this);
}

void ui::canvas::drawBackground(QPainter* painter, const QRectF& rect) {
    painter->fillRect(rect, Qt::white);
    painter->setRenderHint(QPainter::Antialiasing, true);
    draw_grid_lines(painter, rect, k_grid_line_spacing);
}

ui::tool_manager& ui::canvas::tool_mgr() {
    return view().main_window().tool_mgr();
}

ui::canvas_view& ui::canvas::view() {
    return *static_cast<ui::canvas_view*>(this->views()[0]);
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
    setScene(new ui::canvas()); /*
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