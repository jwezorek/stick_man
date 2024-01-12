#include "canvas_item.h"
#include <QWidget>

namespace ui {
    namespace canvas {
        namespace item {
            class rubber_band {
            protected:

                QPointF pinned_point_;

            public: 
                void set_pinned_point(const QPointF pt);
                virtual void handle_drag(const QPointF pt) = 0;
            };
        }
    }
}