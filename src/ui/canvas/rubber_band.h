#include "canvas_item.h"
#include <QWidget>

namespace ui {
    namespace canvas {
        namespace item {
            class rubber_band {
            protected:

                QPointF pinned_point_;

            public: 
                void set_pinned_point(const QPointF& pt);
                virtual void handle_drag(const QPointF& pt) = 0;
                virtual ~rubber_band() {}
            };

            class rect_rubber_band : public rubber_band, public QGraphicsRectItem {
            public:
                rect_rubber_band(const QPointF& pt);
                void handle_drag(const QPointF& pt);
            };
        }
    }
}