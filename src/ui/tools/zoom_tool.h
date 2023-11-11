#pragma once

#include "tool.h"

namespace ui {

    class zoom_tool : public tool {
        int zoom_level_;
        QWidget* settings_;
        QComboBox* magnify_;
        static qreal scale_from_zoom_level(int zl);
        void handleButtonClick(int level);

    public:
        zoom_tool();
        void mouseReleaseEvent(canvas& c, QGraphicsSceneMouseEvent* event) override;
        virtual QWidget* settings_widget() override;
    };

}