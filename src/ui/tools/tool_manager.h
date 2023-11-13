#pragma once

#include "tool.h"

namespace ui {

    namespace canvas {
        class manager;
    }

    namespace tool {
        class tool_manager : public QObject, public input_handler {

            Q_OBJECT

        private:
            std::vector<std::unique_ptr<base>> tool_registry_;
            int curr_item_index_;

            int index_from_id(id id) const;

        public:
            tool_manager();
            void init(canvas::manager& canvases, mdl::project& model);
            void keyPressEvent(canvas::scene& c, QKeyEvent* event) override;
            void keyReleaseEvent(canvas::scene& c, QKeyEvent* event) override;
            void mousePressEvent(canvas::scene& c, QGraphicsSceneMouseEvent* event) override;
            void mouseMoveEvent(canvas::scene& c, QGraphicsSceneMouseEvent* event) override;
            void mouseReleaseEvent(canvas::scene& c, QGraphicsSceneMouseEvent* event) override;
            void mouseDoubleClickEvent(canvas::scene& c, QGraphicsSceneMouseEvent* event) override;
            void wheelEvent(canvas::scene& c, QGraphicsSceneWheelEvent* event) override;
            std::span<const fields> tool_info() const;
            bool has_current_tool() const;
            base& current_tool() const;
            void set_current_tool(canvas::manager& canvases, id id);
        signals:
            void current_tool_changed(base& new_tool);
        };
    }
}