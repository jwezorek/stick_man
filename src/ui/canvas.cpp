#include "canvas.h"
#include <boost/geometry.hpp>
#include <ranges>

namespace r = std::ranges;
namespace rv = std::ranges::views;

/*------------------------------------------------------------------------------------------------*/

namespace {

    void draw_grid_lines(QPainter& painter, const QRect& r, double line_spacing) {
        QPen blue_pen(QColor::fromCmykF(0.4, 0, 0, 0));
        painter.setPen(blue_pen);

        int x1, y1, x2, y2;
        r.getCoords(&x1, &y1, &x2, &y2);

        int left_gridline_index = static_cast<int>(std::ceil(x1 / line_spacing));
        int right_gridline_index = static_cast<int>(std::floor(x2 / line_spacing));
        for (auto i : rv::iota(left_gridline_index, right_gridline_index + 1)) {
            int x = static_cast<int>(i * line_spacing);
            painter.drawLine(x, y1, x, y2);
        }

        int top_gridline_index = static_cast<int>(std::ceil(y1 / line_spacing));
        int bottom_gridline_index = static_cast<int>(std::floor(y2 / line_spacing));
        for (auto i : rv::iota(top_gridline_index, bottom_gridline_index + 1)) {
            int y = static_cast<int>(i * line_spacing);
            painter.drawLine(x1, y, x2, y);
        }
    }
}

ui::canvas::canvas() {
}

void ui::canvas::paintEvent(QPaintEvent* event) {
    auto dirty_rect = event->rect();

    QPainter painter(this);
    painter.fillRect(dirty_rect, Qt::white);
    painter.setRenderHint(QPainter::Antialiasing, true);

    auto line_spacing = k_grid_line_spacing * scale_;
    draw_grid_lines(painter, dirty_rect, line_spacing);
    
}

void ui::canvas::keyPressEvent(QKeyEvent* event) {

}

void ui::canvas::mousePressEvent(QMouseEvent* event) {

}

void ui::canvas::mouseMoveEvent(QMouseEvent* event) {

}

void ui::canvas::mouseReleaseEvent(QMouseEvent* event) {

}

