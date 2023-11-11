#pragma once

#include "tool.h"

namespace ui {

    class tool_manager : public QObject, public input_handler {

        Q_OBJECT

    private:
        std::vector<std::unique_ptr<ui::tool>> tool_registry_;
        int curr_item_index_;

        int index_from_id(tool_id id) const;

    public:
        tool_manager();
        void init(canvas_manager& canvases, mdl::project& model);
        void keyPressEvent(canvas& c, QKeyEvent* event) override;
        void keyReleaseEvent(canvas& c, QKeyEvent* event) override;
        void mousePressEvent(canvas& c, QGraphicsSceneMouseEvent* event) override;
        void mouseMoveEvent(canvas& c, QGraphicsSceneMouseEvent* event) override;
        void mouseReleaseEvent(canvas& c, QGraphicsSceneMouseEvent* event) override;
        void mouseDoubleClickEvent(canvas& c, QGraphicsSceneMouseEvent* event) override;
        void wheelEvent(canvas& c, QGraphicsSceneWheelEvent* event) override;
        std::span<const tool_info> tool_info() const;
        bool has_current_tool() const;
        tool& current_tool() const;
        void set_current_tool(canvas_manager& canvases, tool_id id);
    signals:
        void current_tool_changed(tool& new_tool);
    };

}