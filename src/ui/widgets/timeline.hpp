#pragma once
#include <QAbstractScrollArea>
#include <QTimer>
#include <QString>
#include <optional>
#include <vector>

namespace ui {
    enum class timeline_color { blue, green, orange, purple, red, teal, yellow };

    // Generic discrete timeline key.  Animation V2's skeletal pose editing uses
    // Pose Strip; this control remains as a foundation for future artwork/property keys.
    struct timeline_item {
        QString id;
        qint64 time = 0;
        int row = 0;
        QString label;
        timeline_color color = timeline_color::blue;
        bool disabled = false;
    };

    class timeline : public QAbstractScrollArea {
        Q_OBJECT
        std::vector<timeline_item> items_;
        int rows_ = 0, row_height_ = 36;
        qint64 origin_ = 0, head_ = 0, snap_ = 10;
        double pixels_per_ms_ = .1;
        bool snapping_ = false, follow_ = false;
        QString selected_, hovered_;
        std::optional<timeline_item> drag_;
        qint64 grab_offset_ = 0, proposed_time_ = 0;
        int proposed_row_ = 0;
        QPointF pointer_;
        QTimer auto_scroll_;
        static constexpr int gutter = 76, ruler = 30;

        void update_scrollbars();
        QRectF item_rect(const timeline_item& item) const;
        const timeline_item* hit_item(QPoint point) const;
        int row_at(int y) const;
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
        void set_items(std::vector<timeline_item> items);
        void set_selected_item(QString id) { selected_ = std::move(id); viewport()->update(); }
        const std::vector<timeline_item>& items() const { return items_; }
        void set_visible_range(qint64 first, qint64 last);
        qint64 time_at(double x) const;
        double x_at(qint64 time) const;
        void set_head_time(qint64 time);
        qint64 head_time() const { return head_; }
        void set_follow_head(bool follow) { follow_ = follow; }
        void set_snap_interval(qint64 interval) { snap_ = qMax<qint64>(1, interval); }
        void set_snap_enabled(bool enabled) { snapping_ = enabled; }

    signals:
        void headMoved(qint64 time);
        void itemSelected(QString id);
        void itemDoubleClicked(QString id);
        void itemMoveRequested(QString id, qint64 time, int row);
        void itemContextMenuRequested(QString id, QPoint globalPosition);
    };
}
