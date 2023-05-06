#pragma once

#include <QWidget>
#include <QtWidgets>
#include <QGraphicsScene>

/*------------------------------------------------------------------------------------------------*/

namespace ui {

    class canvas : public QGraphicsScene {

        Q_OBJECT

    private:

        constexpr static auto k_grid_line_spacing = 10;
        double scale_ = 1.0;

    public:

        canvas();
        void drawBackground(QPainter* painter, const QRectF& rect) override;

    protected:

        void dragEnterEvent(QGraphicsSceneDragDropEvent* event) override;
        void dragMoveEvent(QGraphicsSceneDragDropEvent* event) override;
        void dragLeaveEvent(QGraphicsSceneDragDropEvent* event) override;
        void keyPressEvent(QKeyEvent* event) override;
        void keyReleaseEvent(QKeyEvent* event) override;
        void mousePressEvent(QGraphicsSceneMouseEvent* event) override;
        void mouseMoveEvent(QGraphicsSceneMouseEvent* event) override;
        void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override;
        void mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event) override;
        void wheelEvent(QGraphicsSceneWheelEvent* event) override;

    };

}