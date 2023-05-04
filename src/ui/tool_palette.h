#pragma once

#include <QWidget>
#include <QtWidgets>

/*------------------------------------------------------------------------------------------------*/

namespace ui {

    class tool_palette : public QDockWidget {

        Q_OBJECT

    private:

        constexpr static auto k_tool_dim = 64;
        bool is_vertical_;

    public:

        tool_palette(QMainWindow* wnd, bool vertical);
        bool is_vertical() const;
        void set_orientation(bool vert);

    signals:
        void selected_tool_changed(int id);

    };

}