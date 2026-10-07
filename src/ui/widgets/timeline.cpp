#include "timeline.hpp"
#include <QtWidgets>
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace {
QColor color(ui::timeline_color c) {
    static const QColor colors[] = {QColor("#538fc5"), QColor("#65a879"), QColor("#cf9654"),
        QColor("#a17ac2"), QColor("#c97176"), QColor("#53aaa8"), QColor("#c6b65d")};
    return colors[std::clamp(int(c), 0, 6)];
}
}

ui::timeline::timeline(QWidget* parent) : QAbstractScrollArea(parent) {
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setMinimumSize(200, 100);
    connect(horizontalScrollBar(), &QScrollBar::valueChanged, viewport(), qOverload<>(&QWidget::update));
    connect(verticalScrollBar(), &QScrollBar::valueChanged, viewport(), qOverload<>(&QWidget::update));
    auto_scroll_.setInterval(30);
    connect(&auto_scroll_, &QTimer::timeout, this, [this] {
        if (!drag_)
            return;
        auto* h = horizontalScrollBar();
        auto* v = verticalScrollBar();
        if (pointer_.x() > viewport()->width() - 24)
            h->setValue(h->value() + 12);
        if (pointer_.x() < gutter + 12)
            h->setValue(h->value() - 12);
        if (pointer_.y() > viewport()->height() - 16)
            v->setValue(v->value() + 8);
        if (pointer_.y() < ruler + 12)
            v->setValue(v->value() - 8);
        update_drag(pointer_);
    });
}

void ui::timeline::set_rows(int count) {
    rows_ = qMax(0, count);
    update_scrollbars();
}

void ui::timeline::set_items(std::vector<timeline_item> items) {
    QSet<QString> ids;
    for (const auto& item : items) {
        if (item.id.isEmpty() || ids.contains(item.id) || item.time < 0 ||
            item.row < 0 || item.row >= rows_)
            throw std::invalid_argument("Invalid timeline item");
        ids.insert(item.id);
    }
    cancel_drag();
    items_ = std::move(items);
    if (!ids.contains(selected_))
        selected_.clear();
    update_scrollbars();
}

qint64 ui::timeline::time_at(double x) const {
    long double value = origin_ + (x - gutter + horizontalScrollBar()->value()) / pixels_per_ms_;
    return qint64(std::round(std::clamp(value, 0.0L,
        static_cast<long double>(std::numeric_limits<qint64>::max() - 1024))));
}

double ui::timeline::x_at(qint64 time) const {
    return gutter + double(time - origin_) * pixels_per_ms_ - horizontalScrollBar()->value();
}

qint64 ui::timeline::snapped(qint64 time) const {
    if (!snapping_)
        return time;
    const auto rest = time % snap_;
    return rest >= snap_ - rest && time <= INT64_MAX - (snap_ - rest)
        ? time + (snap_ - rest) : time - rest;
}

void ui::timeline::set_head_time(qint64 time) {
    head_ = qMax<qint64>(0, time);
    if (follow_ && (x_at(head_) < gutter || x_at(head_) > viewport()->width() - 30)) {
        update_scrollbars();
        horizontalScrollBar()->setValue(int(std::clamp(
            double(head_) * pixels_per_ms_ - (viewport()->width() - gutter) * .7,
            0.0, double(INT_MAX - 1))));
    }
    viewport()->update();
}

void ui::timeline::update_scrollbars() {
    verticalScrollBar()->setRange(0, qMax(0, rows_ * row_height_ - viewport()->height() + ruler));
    verticalScrollBar()->setPageStep(qMax(1, viewport()->height() - ruler));
    double end = qMax(1000, viewport()->width() * 10);
    end = qMax(end, double(head_) * pixels_per_ms_ + viewport()->width());
    for (const auto& item : items_)
        end = qMax(end, double(item.time - origin_) * pixels_per_ms_ + 40.0);
    horizontalScrollBar()->setRange(0, int(std::clamp(end, 0.0, double(INT_MAX - 1))));
    horizontalScrollBar()->setPageStep(qMax(1, viewport()->width() - gutter));
    viewport()->update();
}

QRectF ui::timeline::item_rect(const timeline_item& item) const {
    const double x = x_at(item.time);
    const double y = ruler + item.row * row_height_ - verticalScrollBar()->value() + row_height_ / 2.0;
    return {x - 7.0, y - 7.0, 14.0, 14.0};
}

const ui::timeline_item* ui::timeline::hit_item(QPoint point) const {
    if (point.x() < gutter || point.y() < ruler)
        return nullptr;
    for (auto it = items_.rbegin(); it != items_.rend(); ++it) {
        auto rect = item_rect(*it).adjusted(-4, -4, 4, 4);
        if (rect.contains(point))
            return &*it;
    }
    return nullptr;
}

int ui::timeline::row_at(int y) const {
    if (rows_ == 0)
        return 0;
    return std::clamp((y - ruler + verticalScrollBar()->value()) / row_height_, 0, rows_ - 1);
}

void ui::timeline::paintEvent(QPaintEvent*) {
    QPainter painter(viewport());
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(viewport()->rect(), palette().base());
    painter.setPen(palette().mid().color());
    for (int row = 0; row <= rows_; ++row) {
        const int y = ruler + row * row_height_ - verticalScrollBar()->value();
        if (y < ruler || y > viewport()->height())
            continue;
        painter.drawLine(0, y, viewport()->width(), y);
        if (row < rows_)
            painter.drawText(QRect(4, y, gutter - 8, row_height_), Qt::AlignCenter, QString::number(row + 1));
    }

    painter.save();
    painter.setClipRect(QRect(gutter, ruler, viewport()->width() - gutter, viewport()->height() - ruler));
    auto draw_item = [&](const timeline_item& item, bool preview) {
        auto rect = item_rect(item);
        if (preview) {
            auto copy = item;
            copy.time = proposed_time_;
            copy.row = proposed_row_;
            rect = item_rect(copy);
        }
        QColor c = item.disabled ? palette().mid().color() : color(item.color);
        if (item.id == selected_)
            c = c.lighter(125);
        painter.setBrush(c);
        painter.setPen(QPen(item.id == selected_ ? palette().highlight().color().lighter(135) : c.darker(140),
            item.id == selected_ ? 2 : 1));
        QPolygonF diamond{QPointF(rect.center().x(), rect.top()), QPointF(rect.right(), rect.center().y()),
            QPointF(rect.center().x(), rect.bottom()), QPointF(rect.left(), rect.center().y())};
        painter.drawPolygon(diamond);
        if (!item.label.isEmpty()) {
            painter.setPen(palette().text().color());
            painter.drawText(QPointF(rect.right() + 5, rect.center().y() + fontMetrics().ascent() / 2.0), item.label);
        }
    };
    for (const auto& item : items_)
        draw_item(item, false);
    if (drag_)
        draw_item(*drag_, true);
    painter.restore();

    painter.fillRect(QRect(gutter, 0, viewport()->width() - gutter, ruler), palette().alternateBase());
    const double raw_step = 90 / pixels_per_ms_;
    const double decade = std::pow(10, std::floor(std::log10(qMax(1.0, raw_step))));
    double major = decade;
    for (double factor : { 1.0, 2.0, 5.0, 10.0 })
        if (factor * decade >= raw_step) {
            major = factor * decade;
            break;
        }
    const qint64 step = qMax<qint64>(1, qint64(major / 5));
    const qint64 first = time_at(gutter) / step * step, last = time_at(viewport()->width());
    for (qint64 t = first; t <= last && t <= INT64_MAX - step; t += step) {
        const auto x = x_at(t);
        if (x < gutter)
            continue;
        const bool big = t % qMax<qint64>(1, qint64(major)) == 0;
        painter.setPen(palette().mid().color());
        painter.drawLine(QPointF(x, ruler - (big ? 10 : 4)), QPointF(x, ruler));
        if (big) {
            painter.setPen(palette().text().color());
            painter.drawText(QPointF(x + 4, 16),
                QString::number(t / 1000.0, 'f', major < 100 ? 3 : major < 1000 ? 2 : 1) + " s");
        }
    }
    const double x = x_at(head_);
    if (x >= gutter && x <= viewport()->width()) {
        painter.setPen(QPen(QColor("#df665f"), 2));
        painter.drawLine(QPointF(x, 0), QPointF(x, viewport()->height()));
    }
}

void ui::timeline::resizeEvent(QResizeEvent* event) {
    QAbstractScrollArea::resizeEvent(event);
    update_scrollbars();
}

void ui::timeline::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton)
        return;
    setFocus();
    pointer_ = event->position();
    if (pointer_.y() < ruler) {
        set_head_time(snapped(time_at(pointer_.x())));
        emit headMoved(head_);
        return;
    }
    if (auto* item = hit_item(pointer_.toPoint())) {
        selected_ = item->id;
        emit itemSelected(selected_);
        if (item->disabled) {
            viewport()->update();
            return;
        }
        drag_ = *item;
        grab_offset_ = time_at(pointer_.x()) - item->time;
        proposed_time_ = item->time;
        proposed_row_ = item->row;
        auto_scroll_.start();
    } else {
        selected_.clear();
        emit itemSelected({});
    }
    viewport()->update();
}

void ui::timeline::mouseDoubleClickEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton)
        if (auto* item = hit_item(event->position().toPoint())) {
            selected_ = item->id;
            emit itemSelected(selected_);
            emit itemDoubleClicked(selected_);
            viewport()->update();
            event->accept();
            return;
        }
    QAbstractScrollArea::mouseDoubleClickEvent(event);
}

void ui::timeline::update_drag(QPointF position) {
    if (!drag_)
        return;
    proposed_time_ = snapped(qMax<qint64>(0, time_at(position.x()) - grab_offset_));
    proposed_row_ = row_at(int(position.y()));
    viewport()->setCursor(Qt::ClosedHandCursor);
    viewport()->update();
}

void ui::timeline::mouseMoveEvent(QMouseEvent* event) {
    pointer_ = event->position();
    if (drag_)
        update_drag(pointer_);
    else {
        auto* item = hit_item(pointer_.toPoint());
        hovered_ = item ? item->id : QString{};
        viewport()->setCursor(item && !item->disabled ? Qt::OpenHandCursor : Qt::ArrowCursor);
        viewport()->update();
    }
}

void ui::timeline::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton || !drag_)
        return;
    update_drag(event->position());
    const auto id = drag_->id;
    const auto time = proposed_time_;
    const auto row = proposed_row_;
    cancel_drag();
    emit itemMoveRequested(id, time, row);
}

void ui::timeline::cancel_drag() {
    auto_scroll_.stop();
    drag_.reset();
    viewport()->unsetCursor();
    viewport()->update();
}

void ui::timeline::wheelEvent(QWheelEvent* event) {
    if (event->modifiers() & Qt::ControlModifier) {
        const auto anchor = time_at(event->position().x());
        pixels_per_ms_ = std::clamp(pixels_per_ms_ * std::pow(1.2, event->angleDelta().y() / 120.0), .0001, 100.0);
        update_scrollbars();
        const int offset = int(std::clamp(double(anchor) * pixels_per_ms_ - (event->position().x() - gutter),
            0.0, double(INT_MAX - 1)));
        horizontalScrollBar()->setMaximum(qMax(horizontalScrollBar()->maximum(), offset));
        horizontalScrollBar()->setValue(offset);
    } else if (event->modifiers() & Qt::ShiftModifier) {
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - event->angleDelta().y());
    } else {
        verticalScrollBar()->setValue(verticalScrollBar()->value() - event->angleDelta().y());
    }
    event->accept();
}

void ui::timeline::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape) {
        cancel_drag();
        return;
    }
    if (event->key() == Qt::Key_Left || event->key() == Qt::Key_Right) {
        for (const auto& item : items_)
            if (item.id == selected_ && !item.disabled) {
                const auto delta =
                    (event->key() == Qt::Key_Left ? -1 : 1) * (snapping_ ? snap_ : 1);
                emit itemMoveRequested(item.id, qMax<qint64>(0, item.time + delta), item.row);
                return;
            }
    }
    QAbstractScrollArea::keyPressEvent(event);
}

void ui::timeline::contextMenuEvent(QContextMenuEvent* event) {
    if (auto* item = hit_item(event->pos())) emit itemContextMenuRequested(item->id, event->globalPos());
}

void ui::timeline::leaveEvent(QEvent* event) {
    hovered_.clear();
    viewport()->update();
    QAbstractScrollArea::leaveEvent(event);
}
