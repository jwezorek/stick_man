#pragma once

#include "tool.h"

namespace ui {

    namespace tool {
        class zoom : public base {
            int zoom_level_;
            QWidget* settings_;
            QComboBox* magnify_;
            static qreal scale_from_zoom_level(int zl);
            void handleButtonClick(int level);

        public:
            zoom();
            void mouseReleaseEvent(canvas::scene& c, QGraphicsSceneMouseEvent* event) override;
            virtual QWidget* settings_widget() override;
        };
    }

}