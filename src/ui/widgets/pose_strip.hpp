#pragma once

#include <QWidget>

namespace ui {

class pose_strip : public QWidget {
    Q_OBJECT
public:
    explicit pose_strip(QWidget* parent = nullptr);

protected:
    void paintEvent(QPaintEvent* event) override;
};

} // namespace ui
