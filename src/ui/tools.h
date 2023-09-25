#pragma once

#include "canvas.h"
#include <QWidget>
#include <QtWidgets>
#include <vector>
#include <memory>
#include <span>
#include <optional>
#include <functional>

/*------------------------------------------------------------------------------------------------*/

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
    class stick_man;
    class tool_manager;

    class abstract_tool : public input_handler {
    public:
        abstract_tool(QString name, QString rsrc, tool_id id);
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
		virtual void init();
        virtual QWidget* settings_widget();
		virtual ~abstract_tool();

    private:
        tool_id id_;
        QString name_;
        QString rsrc_;
    };

    class zoom_tool : public abstract_tool {
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

    class pan_tool : public abstract_tool {
    public:
        pan_tool();
        void activate(canvas_manager& canvases) override;
        void deactivate(canvas_manager& canvases) override;
    };

    class add_node_tool : public abstract_tool {
        sm::world& model_;
    public:
        add_node_tool(sm::world& model);
        void mouseReleaseEvent(canvas& c, QGraphicsSceneMouseEvent* event) override;
    };

    class add_bone_tool : public abstract_tool {
    private:
        QPointF origin_;
        QGraphicsLineItem* rubber_band_;
        sm::world& model_;

        void init_rubber_band(canvas& c);

    public:
        add_bone_tool(sm::world& model);
        void mousePressEvent(canvas& c, QGraphicsSceneMouseEvent* event) override;
        void mouseMoveEvent(canvas& c, QGraphicsSceneMouseEvent* event) override;
        void mouseReleaseEvent(canvas& c, QGraphicsSceneMouseEvent* event) override;
    };

    class tool_manager : public input_handler {
    private:
        std::vector<std::unique_ptr<ui::abstract_tool>> tool_registry_;
        stick_man& main_window_;
        int curr_item_index_;

        int index_from_id(tool_id id) const;

    public:
        tool_manager(stick_man* c);
		void init();
        void keyPressEvent(canvas& c, QKeyEvent* event) override;
        void keyReleaseEvent(canvas& c, QKeyEvent* event) override;
        void mousePressEvent(canvas& c, QGraphicsSceneMouseEvent* event) override;
        void mouseMoveEvent(canvas& c, QGraphicsSceneMouseEvent* event) override;
        void mouseReleaseEvent(canvas& c, QGraphicsSceneMouseEvent* event) override;
        void mouseDoubleClickEvent(canvas& c, QGraphicsSceneMouseEvent* event) override;
        void wheelEvent(canvas& c, QGraphicsSceneWheelEvent* event) override;
        std::span<const tool_info> tool_info() const;
        bool has_current_tool() const;
        abstract_tool& current_tool() const;
        void set_current_tool(canvas_manager& canvases, tool_id id);
    };

}