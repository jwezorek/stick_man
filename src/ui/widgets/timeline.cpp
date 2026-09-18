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
    setFocusPolicy(Qt::StrongFocus); setMouseTracking(true);
    setMinimumSize(200, 100);
    connect(horizontalScrollBar(), &QScrollBar::valueChanged, viewport(), qOverload<>(&QWidget::update));
    connect(verticalScrollBar(), &QScrollBar::valueChanged, viewport(), qOverload<>(&QWidget::update));
    auto_scroll_.setInterval(30);
    connect(&auto_scroll_, &QTimer::timeout, this, [this] {
        if (gesture_ == gesture::none) return;
        auto* h = horizontalScrollBar(); auto* v = verticalScrollBar();
        if (pointer_.x() > viewport()->width()-24) h->setValue(h->value()+12);
        if (pointer_.x() < gutter+12) h->setValue(h->value()-12);
        if (pointer_.y() > viewport()->height()-16) v->setValue(v->value()+8);
        if (pointer_.y() < ruler+12) v->setValue(v->value()-8);
        update_drag(pointer_);
    });
}
void ui::timeline::set_rows(int count) { rows_ = qMax(0, count); set_row_head(row_head_); update_scrollbars(); }
void ui::timeline::set_row_height(int height) { row_height_ = qMax(20, height); update_scrollbars(); }
void ui::timeline::set_items(std::vector<timeline_item> items) {
    QSet<QString> ids;
    for (const auto& i : items) {
        if (i.id.isEmpty() || ids.contains(i.id) || i.start < 0 || i.duration <= 0 ||
            i.start > std::numeric_limits<qint64>::max()-i.duration || i.row < 0 || i.row >= rows_)
            throw std::invalid_argument("Invalid timeline item");
        ids.insert(i.id);
    }
    cancel_drag(); ++revision_; items_ = std::move(items);
    if (!ids.contains(selected_)) selected_.clear();
    update_scrollbars();
}
void ui::timeline::set_visible_range(qint64 first, qint64 last) {
    if (first < 0 || last <= first) throw std::invalid_argument("Invalid visible time range");
    origin_ = 0;
    pixels_per_ms_ = double(qMax(1, viewport()->width()-gutter)) / double(last-first);
    update_scrollbars();
    const int offset = int(std::min(double(INT_MAX-1), double(first)*pixels_per_ms_));
    horizontalScrollBar()->setMaximum(qMax(horizontalScrollBar()->maximum(), offset));
    horizontalScrollBar()->setValue(offset);
}
qint64 ui::timeline::time_at(double x) const {
    long double value = origin_ + (x-gutter+horizontalScrollBar()->value())/pixels_per_ms_;
    return qint64(std::round(std::clamp(value, 0.0L, static_cast<long double>(std::numeric_limits<qint64>::max()-1024))));
}
double ui::timeline::x_at(qint64 t) const { return gutter + double(t-origin_)*pixels_per_ms_ - horizontalScrollBar()->value(); }
qint64 ui::timeline::snapped(qint64 t) const {
    if (!snapping_) return t;
    const auto rest = t % snap_;
    return rest >= snap_-rest && t <= INT64_MAX-(snap_-rest) ? t+(snap_-rest) : t-rest;
}
void ui::timeline::set_head_time(qint64 t) {
    head_ = qMax<qint64>(0, t);
    if (follow_ && (x_at(head_) < gutter || x_at(head_) > viewport()->width()-30)) {
        update_scrollbars();
        horizontalScrollBar()->setValue(int(std::clamp(double(head_)*pixels_per_ms_-(viewport()->width()-gutter)*.7, 0.0, double(INT_MAX-1))));
    }
    viewport()->update();
}
void ui::timeline::set_row_head(row_head_position p) {
    if (rows_ == 0) p = {};
    p.index = std::clamp(p.index, 0, p.kind == row_head_position::placement::on_row ? qMax(0, rows_-1) : rows_);
    row_head_ = p; viewport()->update();
}
void ui::timeline::update_scrollbars() {
    verticalScrollBar()->setRange(0, qMax(0, rows_*row_height_ - viewport()->height()+ruler));
    verticalScrollBar()->setPageStep(qMax(1, viewport()->height()-ruler));
    double end = qMax(1000, viewport()->width()*10);
    end = qMax(end, double(head_)*pixels_per_ms_+viewport()->width());
    for (const auto& i : items_) end = qMax(end, double(i.start+i.duration-origin_)*pixels_per_ms_);
    horizontalScrollBar()->setRange(0, int(std::clamp(end, 0.0, double(INT_MAX-1))));
    horizontalScrollBar()->setPageStep(qMax(1, viewport()->width()-gutter));
    viewport()->update();
}
QRectF ui::timeline::item_rect(const timeline_item& i) const {
    return {x_at(i.start), double(ruler+i.row*row_height_-verticalScrollBar()->value()+5),
        qMax(1.0, double(i.duration)*pixels_per_ms_), double(row_height_-10)};
}
QRectF ui::timeline::row_head_hit_rect() const {
    const double x = x_at(head_);
    double y = ruler + row_head_.index*row_height_ - verticalScrollBar()->value();
    if (row_head_.kind == row_head_position::placement::on_row) y += row_height_/2.0;
    // The painted triangle is only 10x10. Give it a slightly larger target so
    // it remains easy to grab when it overlaps an action rectangle.
    return {x-14, y-9, 20, 18};
}
bool ui::timeline::hit_row_head(QPointF p) const {
    const double x = x_at(head_);
    return x >= gutter && x <= viewport()->width() && p.y() >= ruler && row_head_hit_rect().contains(p);
}
const ui::timeline_item* ui::timeline::hit_item(QPoint p) const {
    if (p.x() < gutter || p.y() < ruler) return nullptr;
    for (auto i = items_.rbegin(); i != items_.rend(); ++i) {
        // Edge handles need the same tolerance on either side of their painted edge.
        auto r = item_rect(*i); r.adjust(-4, 0, 4, 0);
        if (r.contains(p)) return &*i;
    }
    return nullptr;
}
ui::row_head_position ui::timeline::row_at(int y) const {
    const double row = double(y-ruler+verticalScrollBar()->value())/row_height_;
    const int boundary = int(std::round(row));
    if (rows_ == 0 || std::abs(row-boundary)*row_height_ <= 6 || row < 0 || row >= rows_)
        return {row_head_position::placement::between_rows, std::clamp(boundary, 0, rows_)};
    return {row_head_position::placement::on_row, std::clamp(int(row), 0, rows_-1)};
}
void ui::timeline::paintEvent(QPaintEvent*) {
    QPainter p(viewport()); p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(viewport()->rect(), palette().base());
    p.setPen(palette().mid().color());
    for (int row = 0; row <= rows_; ++row) {
        int y = ruler+row*row_height_-verticalScrollBar()->value();
        if (y < ruler || y > viewport()->height()) continue;
        p.drawLine(0, y, viewport()->width(), y);
        if (row < rows_) p.drawText(QRect(4, y, gutter-8, row_height_), Qt::AlignCenter, QString::number(row+1));
    }
    p.save(); p.setClipRect(QRect(gutter, ruler, viewport()->width()-gutter, viewport()->height()-ruler));
    auto draw = [&](const timeline_item& i, bool preview) {
        auto r = item_rect(i); QColor c = color(i.color);
        if (i.disabled) c = palette().mid().color();
        QLinearGradient gradient(r.topLeft(), r.topRight()); gradient.setColorAt(0, c.lighter(115)); gradient.setColorAt(1, c.darker(108));
        p.setBrush(gradient);
        p.setPen(QPen(i.invalid || (preview && !valid_drop_) ? QColor("#e45a64") :
            i.id == selected_ ? palette().highlight().color() : c.darker(140), i.id == selected_ || preview ? 2 : 1,
            i.provisional || preview ? Qt::DashLine : Qt::SolidLine));
        p.drawRoundedRect(r, 3, 3);
        if (i.id == hovered_) p.fillRect(r, QColor(255,255,255,35));
        if (r.width() < 8) { p.setPen(QPen(c.lighter(140), 2)); p.drawLine(r.topLeft(), r.bottomLeft()); }
        p.setPen(Qt::white); p.drawText(r.adjusted(5,0,-4,0), Qt::AlignVCenter,
            fontMetrics().elidedText(i.label, Qt::ElideRight, qMax(0, int(r.width()-9))));
    };
    for (const auto& i : items_) draw(i, false);
    if (drag_) { auto i = *drag_; i.start = proposed_start_; i.duration = proposed_end_-proposed_start_; i.row = proposed_row_.index; draw(i, true); }
    p.restore();
    p.fillRect(QRect(gutter, 0, viewport()->width()-gutter, ruler), palette().alternateBase());
    const double raw_step = 90/pixels_per_ms_;
    const double decade = std::pow(10, std::floor(std::log10(qMax(1.0, raw_step))));
    double major = decade;
    for (double factor : {1.0,2.0,5.0,10.0}) if (factor*decade >= raw_step) { major = factor*decade; break; }
    const qint64 step = qMax<qint64>(1, qint64(major/5));
    const qint64 first = time_at(gutter)/step*step, last = time_at(viewport()->width());
    for (qint64 t = first; t <= last && t <= INT64_MAX-step; t += step) {
        const auto x = x_at(t); if (x < gutter) continue;
        const bool big = t % qMax<qint64>(1,qint64(major)) == 0;
        p.setPen(palette().mid().color()); p.drawLine(QPointF(x, ruler-(big?10:4)), QPointF(x, ruler));
        if (big) { p.setPen(palette().text().color()); p.drawText(QPointF(x+4, 16), QString::number(t/1000.0, 'f', major < 100 ? 3 : major < 1000 ? 2 : 1)+" s"); }
    }
    const double x = x_at(head_);
    if (x >= gutter && x <= viewport()->width()) {
        p.setPen(QPen(QColor("#df665f"), 2)); p.drawLine(QPointF(x, 0), QPointF(x, viewport()->height()));
        double y = ruler + row_head_.index*row_height_ - verticalScrollBar()->value();
        if (row_head_.kind == row_head_position::placement::on_row) y += row_height_/2.0;
        p.setBrush(QColor("#df665f")); p.drawPolygon(QPolygonF{QPointF(x-10,y-5), QPointF(x,y), QPointF(x-10,y+5)});
    }
}
void ui::timeline::resizeEvent(QResizeEvent* e) { QAbstractScrollArea::resizeEvent(e); update_scrollbars(); }
void ui::timeline::mousePressEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton) return;
    setFocus(); pointer_ = e->position();
    if (pointer_.y() < ruler) gesture_ = gesture::head;
    else if (hit_row_head(pointer_)) {
        selected_.clear(); emit itemSelected({});
        gesture_ = gesture::row_head;
        viewport()->setCursor(Qt::SizeVerCursor);
    }
    else if (auto i = hit_item(pointer_.toPoint())) {
        const auto clicked = *i;
        const auto revision = revision_;
        selected_ = clicked.id; emit itemSelected(selected_);
        if (revision != revision_ || clicked.disabled) { viewport()->update(); return; }
        drag_ = clicked; grab_time_ = time_at(pointer_.x())-clicked.start;
        auto r = item_rect(clicked);
        gesture_ = std::abs(pointer_.x()-r.left()) < 5 ? gesture::left : std::abs(pointer_.x()-r.right()) < 5 ? gesture::right : gesture::move;
    } else {
        selected_.clear(); emit itemSelected({}); gesture_ = gesture::row_head;
    }
    update_drag(pointer_); auto_scroll_.start();
}
void ui::timeline::mouseDoubleClickEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton) return;
    if (hit_row_head(e->position())) { e->accept(); return; }
    if (auto* i = hit_item(e->position().toPoint())) {
        selected_ = i->id;
        emit itemSelected(selected_);
        emit itemDoubleClicked(selected_);
        viewport()->update();
        e->accept();
        return;
    }
    QAbstractScrollArea::mouseDoubleClickEvent(e);
}
void ui::timeline::update_drag(QPointF pos) {
    if (gesture_ == gesture::head) { set_head_time(snapped(time_at(pos.x()))); emit headMoved(head_); }
    else if (gesture_ == gesture::row_head) { set_row_head(row_at(pos.y())); emit rowHeadMoved(row_head_); }
    else if (drag_) {
        proposed_row_ = gesture_ == gesture::move ? row_at(pos.y()) : row_head_position{row_head_position::placement::on_row, drag_->row};
        proposed_start_ = drag_->start; proposed_end_ = drag_->start+drag_->duration;
        if (gesture_ == gesture::move) { proposed_start_ = qMin(INT64_MAX-drag_->duration, snapped(qMax<qint64>(0,time_at(pos.x())-grab_time_))); proposed_end_ = proposed_start_+drag_->duration; }
        if (gesture_ == gesture::left) proposed_start_ = qMin(proposed_end_-1, snapped(time_at(pos.x())));
        if (gesture_ == gesture::right) proposed_end_ = qMax(proposed_start_+1, snapped(time_at(pos.x())));
        valid_drop_ = !validator_ || validator_(drag_->id, proposed_start_, proposed_end_, proposed_row_);
        viewport()->setCursor(valid_drop_ ? Qt::ClosedHandCursor : Qt::ForbiddenCursor);
    }
    viewport()->update();
}
void ui::timeline::mouseMoveEvent(QMouseEvent* e) {
    pointer_ = e->position();
    if (gesture_ != gesture::none) update_drag(pointer_);
    else if (hit_row_head(pointer_)) {
        hovered_.clear();
        viewport()->setCursor(Qt::SizeVerCursor);
        viewport()->update();
    } else {
        viewport()->unsetCursor();
        auto* i = hit_item(pointer_.toPoint()); hovered_ = i ? i->id : QString{}; viewport()->update();
    }
}
void ui::timeline::mouseReleaseEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton) return;
    update_drag(e->position());
    // Copy the intention before notifying an owner that may synchronously replace items.
    auto dragged = drag_; auto g = gesture_; auto start = proposed_start_, end = proposed_end_; auto row = proposed_row_; bool valid = valid_drop_;
    cancel_drag();
    if (dragged && valid) {
        if (g == gesture::move) emit itemMoveRequested(dragged->id, start, row);
        else emit itemResizeRequested(dragged->id, start, end);
    }
}
void ui::timeline::cancel_drag() { auto_scroll_.stop(); gesture_ = gesture::none; drag_.reset(); viewport()->unsetCursor(); viewport()->update(); }
void ui::timeline::wheelEvent(QWheelEvent* e) {
    if (e->modifiers() & Qt::ControlModifier) {
        const auto anchor = time_at(e->position().x());
        pixels_per_ms_ = std::clamp(pixels_per_ms_*std::pow(1.2,e->angleDelta().y()/120.0), .0001, 100.0);
        update_scrollbars();
        const int offset = int(std::clamp(double(anchor)*pixels_per_ms_-(e->position().x()-gutter),0.0,double(INT_MAX-1)));
        horizontalScrollBar()->setMaximum(qMax(horizontalScrollBar()->maximum(),offset));
        horizontalScrollBar()->setValue(offset);
    } else if (e->modifiers() & Qt::ShiftModifier) horizontalScrollBar()->setValue(horizontalScrollBar()->value()-e->angleDelta().y());
    else verticalScrollBar()->setValue(verticalScrollBar()->value()-e->angleDelta().y());
    e->accept();
}
void ui::timeline::keyPressEvent(QKeyEvent* e) {
    if (e->key() == Qt::Key_Escape) { cancel_drag(); return; }
    if (e->key() == Qt::Key_Left || e->key() == Qt::Key_Right) {
        for (const auto& i : items_) if (i.id == selected_ && !i.disabled) {
            auto start = qMax<qint64>(0, i.start+(e->key()==Qt::Key_Left ? -1:1)*(snapping_ ? snap_:1));
            row_head_position row{row_head_position::placement::on_row, i.row};
            if (!validator_ || validator_(i.id,start,start+i.duration,row)) emit itemMoveRequested(i.id,start,row);
            return;
        }
    }
    QAbstractScrollArea::keyPressEvent(e);
}
void ui::timeline::contextMenuEvent(QContextMenuEvent* e) { if (auto i = hit_item(e->pos())) emit itemContextMenuRequested(i->id,e->globalPos()); }
void ui::timeline::leaveEvent(QEvent* e) { hovered_.clear(); viewport()->update(); QAbstractScrollArea::leaveEvent(e); }

