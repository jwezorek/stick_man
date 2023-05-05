#pragma once

#include <QWidget>
#include <QtWidgets>

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

    class tool_btn;

    class tool_palette : public QDockWidget {

        Q_OBJECT

    private:

        constexpr static auto k_tool_dim = 64;
        tool_id curr_tool_id_;

        void handle_tool_click(tool_btn* btn);
        tool_btn* tool_from_id(tool_id id);
    public:

        tool_palette(QMainWindow* wnd);
        //QSize sizeHint() const override;
        //QSize minimumSizeHint() const override;

    signals:
        void selected_tool_changed(tool_id id);

    };

}