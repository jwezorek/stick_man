#include "pose_strip.hpp"

#include <QPainter>
#include <QPaintEvent>

ui::pose_strip::pose_strip(QWidget* parent) : QWidget(parent) {
    setObjectName("pose_strip");
    setMinimumHeight(120);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void ui::pose_strip::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.fillRect(rect(), palette().base());

    auto frame = rect().adjusted(10, 10, -10, -10);
    QPen pen(palette().mid().color());
    pen.setStyle(Qt::DashLine);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(frame, 5, 5);

    painter.setPen(palette().text().color());
    painter.drawText(frame, Qt::AlignCenter,
        tr("Pose Strip\nNo animation poses yet"));
}
