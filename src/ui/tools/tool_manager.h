#pragma once

#include "tool.h"

namespace ui {

    namespace canvas {
        class canvas_manager;
    }

    class tool_manager : public QObject, public input_handler {

        Q_OBJECT

    private:
        std::vector<std::unique_ptr<ui::tool>> tool_registry_;
        int curr_item_index_;

        int index_from_id(tool_id id) const;

    public:
        tool_manager();
        void init(canvas::canvas_manager& canvases, mdl::project& model);
        void keyPressEvent(canvas::canvas& c, QKeyEvent* event) override;
        void keyReleaseEvent(canvas::canvas& c, QKeyEvent* event) override;
        void mousePressEvent(canvas::canvas& c, QGraphicsSceneMouseEvent* event) override;
        void mouseMoveEvent(canvas::canvas& c, QGraphicsSceneMouseEvent* event) override;
        void mouseReleaseEvent(canvas::canvas& c, QGraphicsSceneMouseEvent* event) override;
        void mouseDoubleClickEvent(canvas::canvas& c, QGraphicsSceneMouseEvent* event) override;
        void wheelEvent(canvas::canvas& c, QGraphicsSceneWheelEvent* event) override;
        std::span<const tool_info> tool_info() const;
        bool has_current_tool() const;
        tool& current_tool() const;
        void set_current_tool(canvas::canvas_manager& canvases, tool_id id);
    signals:
        void current_tool_changed(tool& new_tool);
    };

}