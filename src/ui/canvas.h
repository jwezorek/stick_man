#pragma once

#include <QWidget>
#include <QtWidgets>

/*------------------------------------------------------------------------------------------------*/

namespace ui {

    class canvas : public QWidget {

        Q_OBJECT

    private:

        constexpr static auto k_grid_line_spacing = 10;
        double scale_ = 1.0;

    public:

        canvas();

        void paintEvent(QPaintEvent* event) override;
        void keyPressEvent(QKeyEvent* event) override;
        void mousePressEvent(QMouseEvent* event) override;
        void mouseMoveEvent(QMouseEvent* event) override;
        void mouseReleaseEvent(QMouseEvent* event) override;
    signals:
        void selection_changed();

    };

}