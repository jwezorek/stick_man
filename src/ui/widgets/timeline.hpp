#pragma once
#include <QAbstractScrollArea>
#include <QTimer>
#include <QString>
#include <functional>
#include <optional>
#include <vector>

namespace ui {
    enum class timeline_color { blue, green, orange, purple, red, teal, yellow };
    struct row_head_position {
        enum class placement { on_row, between_rows };
        placement kind = placement::between_rows;
        int index = 0;
        bool operator==(const row_head_position&) const = default;
    };
    struct timeline_item {
        QString id;
        qint64 start = 0, duration = 1;
        int row = 0;
        QString label;
        timeline_color color = timeline_color::blue;
        bool provisional = false, disabled = false, invalid = false;
    };
    // Generic millisecond timeline. Owns presentation only; edits are requests to its owner.
    class timeline : public QAbstractScrollArea {
        Q_OBJECT
        std::vector<timeline_item> items_;
        int rows_ = 0, row_height_ = 36;
        qint64 origin_ = 0, head_ = 0, snap_ = 10;
        double pixels_per_ms_ = .1;
        bool snapping_ = false, follow_ = false;
        QString selected_, hovered_;
        unsigned revision_ = 0;
        row_head_position row_head_;
        enum class gesture { none, head, row_head, move, left, right };
        gesture gesture_ = gesture::none;
        std::optional<timeline_item> drag_;
        qint64 grab_time_ = 0, proposed_start_ = 0, proposed_end_ = 0;
        row_head_position proposed_row_;
        QPointF pointer_;
        bool valid_drop_ = true;
        QTimer auto_scroll_;
        std::function<bool(const QString&, qint64, qint64, row_head_position)> validator_;
        static constexpr int gutter = 76, ruler = 30;
        void update_scrollbars();
        QRectF item_rect(const timeline_item& item) const;
        const timeline_item* hit_item(QPoint point) const;
        row_head_position row_at(int y) const;
        qint64 snapped(qint64 time) const;
        void update_drag(QPointF position);
        void cancel_drag();
    protected:
        void paintEvent(QPaintEvent*) override;
        void resizeEvent(QResizeEvent*) override;
        void mousePressEvent(QMouseEvent*) override;
        void mouseDoubleClickEvent(QMouseEvent*) override;
        void mouseMoveEvent(QMouseEvent*) override;
        void mouseReleaseEvent(QMouseEvent*) override;
        void wheelEvent(QWheelEvent*) override;
        void keyPressEvent(QKeyEvent*) override;
        void contextMenuEvent(QContextMenuEvent*) override;
        void leaveEvent(QEvent*) override;
    public:
        explicit timeline(QWidget* parent = nullptr);
        void set_rows(int count);
        void set_row_height(int height);
        void set_items(std::vector<timeline_item> items);
        void set_selected_item(QString id) { selected_ = std::move(id); viewport()->update(); }
        const std::vector<timeline_item>& items() const { return items_; }
        void set_visible_range(qint64 first, qint64 last);
        qint64 time_at(double x) const;
        double x_at(qint64 time) const;
        void set_head_time(qint64 time);
        qint64 head_time() const { return head_; }
        void set_row_head(row_head_position position);
        void set_follow_head(bool follow) { follow_ = follow; }
        void set_snap_interval(qint64 interval) { snap_ = qMax<qint64>(1, interval); }
        void set_snap_enabled(bool enabled) { snapping_ = enabled; }
        void set_drop_validator(std::function<bool(const QString&, qint64, qint64, row_head_position)> validator) { validator_ = std::move(validator); }
    signals:
        void headMoved(qint64 time);
        void rowHeadMoved(ui::row_head_position position);
        void itemSelected(QString id);
        void itemDoubleClicked(QString id);
        void itemMoveRequested(QString id, qint64 start, ui::row_head_position row);
        void itemResizeRequested(QString id, qint64 start, qint64 end);
        void itemContextMenuRequested(QString id, QPoint globalPosition);
    };
}
Q_DECLARE_METATYPE(ui::row_head_position)

