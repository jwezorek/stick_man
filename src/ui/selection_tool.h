#pragma once

#include "tools.h"
#include <QWidget>
#include <QtWidgets>
#include <optional>
#include <unordered_set>

namespace ui {

    using selection_set = std::unordered_set<ui::abstract_stick_man_item*>;

    class selection_tool : public abstract_tool {
    public: 
        enum class modal_state {
            none = 0,
            defining_joint_constraint,
            selecting_anchor_bone
        };
    private:

        bool intitialized_;
        std::optional<QRectF> rubber_band_;
        QMetaObject::Connection conn_;
        QWidget* properties_;
        modal_state state_;

        void handle_click(canvas& c, QPointF pt, bool shift_down, bool alt_down);
        void handle_drag(canvas& c, QRectF rect, bool shift_down, bool alt_down);
        void handle_sel_changed(const selection_set& sel);

    public:

        selection_tool(tool_manager* mgr);
        void activate(canvas& c) override;

        void keyReleaseEvent(canvas& c, QKeyEvent* event) override;
        void mousePressEvent(canvas& c, QGraphicsSceneMouseEvent* event) override;
        void mouseMoveEvent(canvas& c, QGraphicsSceneMouseEvent* event) override;
        void mouseReleaseEvent(canvas& c, QGraphicsSceneMouseEvent* event) override;
        void deactivate(canvas& c) override;
        QWidget* settings_widget() override;
        void set_modal(modal_state state, canvas& c);
        bool is_in_modal_state() const;
    };

}