#pragma once

#include "canvas.h"
#include <QWidget>
#include <QtWidgets>
#include <vector>
#include <memory>
#include <span>
#include <optional>

/*------------------------------------------------------------------------------------------------*/

namespace ui {

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
    class stick_man;
    class tool_manager;

    class abstract_tool {
    public:
        abstract_tool(tool_manager* mgr, QString name, QString rsrc, tool_id id);
        tool_id id() const;
        QString name() const;
        QString icon_rsrc() const;
        tool_manager* manager() const;
        //void set_manager(tool_manager* mgr);
        void populate_settings(tool_settings_pane* pane);
        virtual void activate(canvas& c);
        virtual void keyPressEvent(canvas& c, QKeyEvent* event);
        virtual void keyReleaseEvent(canvas& c, QKeyEvent* event);
        virtual void mousePressEvent(canvas& c, QGraphicsSceneMouseEvent* event);
        virtual void mouseMoveEvent(canvas& c, QGraphicsSceneMouseEvent* event);
        virtual void mouseReleaseEvent(canvas& c, QGraphicsSceneMouseEvent* event);
        virtual void mouseDoubleClickEvent(canvas& c, QGraphicsSceneMouseEvent* event);
        virtual void wheelEvent(canvas& c, QGraphicsSceneWheelEvent* event) ;
        virtual void deactivate(canvas& c);
        virtual QWidget* settings_widget();

    private:
        tool_id id_;
        QString name_;
        QString rsrc_;
        tool_manager* mgr_;
    };

    class zoom_tool : public abstract_tool {
        int zoom_level_;
        QWidget* settings_;
        QComboBox* magnify_;
        static qreal scale_from_zoom_level(int zl);
        void handleButtonClick(int level);


    public:
        zoom_tool(tool_manager* mgr);
        void mouseReleaseEvent(canvas& c, QGraphicsSceneMouseEvent* event) override;
        virtual QWidget* settings_widget() override;
    };

    class pan_tool : public abstract_tool {
    public:
        pan_tool(tool_manager* mgr);
        void activate(canvas& c) override;
        void deactivate(canvas& c) override;
    };

    class add_node_tool : public abstract_tool {
    public:
        add_node_tool(tool_manager* mgr);
        void mouseReleaseEvent(canvas& c, QGraphicsSceneMouseEvent* event) override;
    };

    class add_bone_tool : public abstract_tool {
    private:
        QPointF origin_;
        QGraphicsLineItem* rubber_band_;
    public:
        add_bone_tool(tool_manager* mgr);
        void mousePressEvent(canvas& c, QGraphicsSceneMouseEvent* event) override;
        void mouseMoveEvent(canvas& c, QGraphicsSceneMouseEvent* event) override;
        void mouseReleaseEvent(canvas& c, QGraphicsSceneMouseEvent* event) override;
    };

    class tool_manager {
    private:
        std::vector<std::unique_ptr<ui::abstract_tool>> tool_registry_;
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
        ui::canvas& canvas();
    };

}