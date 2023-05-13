#pragma once

#include "canvas.h"
#include <QWidget>
#include <QtWidgets>
#include <vector>
#include <memory>
#include <span>

/*------------------------------------------------------------------------------------------------*/

namespace ui {

    enum class tool_id {
        none,
        arrow,
        move,
        pan,
        zoom,
        add_joint,
        add_bone
    };

    struct tool_info {
        ui::tool_id id;
        QString name;
        QString rsrc_name;
    };
    class canvas;

    class abstract_tool {
    public:
        abstract_tool(QString name, QString rsrc, tool_id id);
        tool_id id() const;
        QString name() const;
        QString icon_rsrc() const;
        virtual void activate(canvas& c);
        virtual void keyPressEvent(canvas& c, QKeyEvent* event);
        virtual void keyReleaseEvent(canvas& c, QKeyEvent* event);
        virtual void mousePressEvent(canvas& c, QGraphicsSceneMouseEvent* event);
        virtual void mouseMoveEvent(canvas& c, QGraphicsSceneMouseEvent* event);
        virtual void mouseReleaseEvent(canvas& c, QGraphicsSceneMouseEvent* event);
        virtual void mouseDoubleClickEvent(canvas& c, QGraphicsSceneMouseEvent* event);
        virtual void wheelEvent(canvas& c, QGraphicsSceneWheelEvent* event) ;
        virtual void deactivate(canvas& c);


    private:
        tool_id id_;
        QString name_;
        QString rsrc_;
    };

    class zoom_tool : public abstract_tool {
        int zoom_level_;
        qreal scale_from_zoom_level() const;
    public:
        zoom_tool();
        void mouseReleaseEvent(canvas& c, QGraphicsSceneMouseEvent* event) override;
    };

    class pan_tool : public abstract_tool {
    public:
        pan_tool();
        void activate(canvas& c) override;
        void deactivate(canvas& c) override;
    };

    class add_joint_tool : public abstract_tool {
    public:
        add_joint_tool();
        void mouseReleaseEvent(canvas& c, QGraphicsSceneMouseEvent* event) override;
    };

    class add_bone_tool : public abstract_tool {
    private:
        joint_item* parent_joint_;
    public:
        add_bone_tool();
        void mouseReleaseEvent(canvas& c, QGraphicsSceneMouseEvent* event) override;
    };

    class stick_man;

    class tool_manager {
    private:
        stick_man& main_window_;
        int curr_item_index_;

        int index_from_id(tool_id id) const;

    public:
        tool_manager(stick_man* c);
        void keyPressEvent(canvas& c, QKeyEvent* event);
        void keyReleaseEvent(canvas& c, QKeyEvent* event);
        void mousePressEvent(canvas& c, QGraphicsSceneMouseEvent* event);
        void mouseMoveEvent(canvas& c, QGraphicsSceneMouseEvent* event);
        void mouseReleaseEvent(canvas& c, QGraphicsSceneMouseEvent* event);
        void mouseDoubleClickEvent(canvas& c, QGraphicsSceneMouseEvent* event);
        void wheelEvent(canvas& c, QGraphicsSceneWheelEvent* event);
        std::span<const tool_info> tool_info() const;
        bool has_current_tool() const;
        abstract_tool& current_tool() const;
        void set_current_tool(tool_id id);
    };

}