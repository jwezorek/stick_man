#include "zoom_tool.h"

/*------------------------------------------------------------------------------------------------*/

ui::tool::zoom_tool::zoom_tool() :
    base("zoom", "zoom_icon.png", ui::tool::tool_id::zoom),
    zoom_level_(0),
    settings_(nullptr) {
}

qreal ui::tool::zoom_tool::scale_from_zoom_level(int zoom_level) {
    if (zoom_level == 0) {
        return 1.0;
    }
    if (zoom_level > 0) {
        qreal zoom = static_cast<qreal>(zoom_level);
        return zoom + 1.0;
    }
    qreal zoom = -1.0 * zoom_level;
    return 1.0 / (zoom + 1.0);
}

void ui::tool::zoom_tool::mouseReleaseEvent(canvas::scene& c, QGraphicsSceneMouseEvent* event) {
    if (!event->modifiers().testFlag(Qt::AltModifier)) {
        zoom_level_++;
    }
    else {
        zoom_level_--;
    }
    auto pt = event->scenePos();
    auto zoom = scale_from_zoom_level(zoom_level_);
    c.set_scale(zoom, pt);
}

QWidget* ui::tool::zoom_tool::settings_widget() {
    if (!settings_) {
        settings_ = new QWidget();
        auto* flow = new ui::FlowLayout(settings_);
        flow->addWidget(new QLabel("magnify"));
        flow->addWidget(magnify_ = new QComboBox());
        for (int zoom_level = -4; zoom_level <= 4; zoom_level++) {
            int val = static_cast<int>(scale_from_zoom_level(zoom_level) * 100.0);
            QString txt = QString::number(val) + "%";
            magnify_->addItem(txt, zoom_level);
        }
    }
    return settings_;
}