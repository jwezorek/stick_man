#pragma once

#include "../canvas/canvas.h"
#include <QWidget>
#include <QtWidgets>
#include <vector>
#include <memory>
#include <span>
#include <optional>
#include <functional>

/*------------------------------------------------------------------------------------------------*/

namespace mdl {
    class project;
}

namespace ui {

    class input_handler {
    public:
        virtual void keyPressEvent(canvas& c, QKeyEvent* event) = 0;
        virtual void keyReleaseEvent(canvas& c, QKeyEvent* event) = 0;
        virtual void mousePressEvent(canvas& c, QGraphicsSceneMouseEvent* event) = 0;
        virtual void mouseMoveEvent(canvas& c, QGraphicsSceneMouseEvent* event) = 0;
        virtual void mouseReleaseEvent(canvas& c, QGraphicsSceneMouseEvent* event) = 0;
        virtual void mouseDoubleClickEvent(canvas& c, QGraphicsSceneMouseEvent* event) = 0;
        virtual void wheelEvent(canvas& c, QGraphicsSceneWheelEvent* event) = 0;
    };

    enum class tool_id {
        none,
        selection,
        move,
        pan,
        zoom,
        add_node,
        add_bone
    };

    struct tool_info {
        ui::tool_id id;
        QString name;
        QString rsrc_name;
    };

    class tool_settings_pane;
    class canvas_manager;
    class tool_manager;

    class tool : public input_handler {
    public:
        tool(QString name, QString rsrc, tool_id id);
        tool_id id() const;
        QString name() const;
        QString icon_rsrc() const;
        void populate_settings(tool_settings_pane* pane);
        virtual void activate(canvas_manager& canvases);
        virtual void keyPressEvent(canvas& c, QKeyEvent* event) override;
        virtual void keyReleaseEvent(canvas& c, QKeyEvent* event) override;
        virtual void mousePressEvent(canvas& c, QGraphicsSceneMouseEvent* event) override;
        virtual void mouseMoveEvent(canvas& c, QGraphicsSceneMouseEvent* event) override;
        virtual void mouseReleaseEvent(canvas& c, QGraphicsSceneMouseEvent* event) override;
        virtual void mouseDoubleClickEvent(canvas& c, QGraphicsSceneMouseEvent* event) override;
        virtual void wheelEvent(canvas& c, QGraphicsSceneWheelEvent* event) override;
        virtual void deactivate(canvas_manager& canvases);
		virtual void init(canvas_manager& canvases, mdl::project& model);
        virtual QWidget* settings_widget();
		virtual ~tool();

    private:
        tool_id id_;
        QString name_;
        QString rsrc_;
    };

}