#pragma once

#include <QWidget>
#include <QtWidgets>
#include <QGraphicsScene>

/*------------------------------------------------------------------------------------------------*/

namespace ui {

    class canvas_view; 
    class stick_man;
    class tool_manager;

    class canvas : public QGraphicsScene {

        Q_OBJECT

    private:

        constexpr static auto k_grid_line_spacing = 10;
        double scale_ = 1.0;

        tool_manager& tool_mgr();

    public:

        canvas();
        void drawBackground(QPainter* painter, const QRectF& rect) override;
        canvas_view& view();

    protected:

        void keyPressEvent(QKeyEvent* event) override;
        void keyReleaseEvent(QKeyEvent* event) override;
        void mousePressEvent(QGraphicsSceneMouseEvent* event) override;
        void mouseMoveEvent(QGraphicsSceneMouseEvent* event) override;
        void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override;
        void mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event) override;
        void wheelEvent(QGraphicsSceneWheelEvent* event) override;

    };

    class canvas_view : public QGraphicsView {
    private:
        stick_man* main_window_;
    public:
        canvas_view();
        canvas& canvas();
        stick_man& main_window();
    };

}