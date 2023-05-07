#pragma once

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
        virtual void handle_key_press(canvas* c, QKeyEvent* event) = 0;
        virtual void handle_mouse_press(canvas* c, QMouseEvent* event) = 0;
        virtual void handle_mouse_move(canvas* c, QMouseEvent* event) = 0;
        virtual void handle_mouse_release(canvas* c, QMouseEvent* event) = 0;
    private:
        tool_id id_;
        QString name_;
        QString rsrc_;
    };

    class zoom_tool : public abstract_tool {
    public:
        zoom_tool();
        void handle_key_press(canvas* c, QKeyEvent* event) override;
        void handle_mouse_press(canvas* c, QMouseEvent* event) override;
        void handle_mouse_move(canvas* c, QMouseEvent* event) override;
        void handle_mouse_release(canvas* c, QMouseEvent* event) override;
    };

    class pan_tool : public abstract_tool {
    public:
        pan_tool();
        void handle_key_press(canvas* c, QKeyEvent* event) override;
        void handle_mouse_press(canvas* c, QMouseEvent* event) override;
        void handle_mouse_move(canvas* c, QMouseEvent* event) override;
        void handle_mouse_release(canvas* c, QMouseEvent* event) override;
    };

    class stick_man;

    class tool_manager {
    private:
        stick_man& main_window_;
        int curr_item_index_;
    public:
        tool_manager(stick_man* c);
        void handle_key_press(QKeyEvent* event) ;
        void handle_mouse_press(QMouseEvent* event) ;
        void handle_mouse_move( QMouseEvent* event) ;
        void handle_mouse_release(QMouseEvent* event);
        std::span<const tool_info> tool_info() const;
        bool has_current_tool() const;
        abstract_tool& current_tool() const;
        void set_current_tool(tool_id id);
    };

}