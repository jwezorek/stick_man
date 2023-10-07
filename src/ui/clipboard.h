#pragma once

#include <QtWidgets>

/*------------------------------------------------------------------------------------------------*/

namespace ui {

    class canvas;
    class stick_man;

    QByteArray cut_selection(stick_man& canv);
    QByteArray copy_selection(stick_man& canv);
    void delete_selection(stick_man& canv);
    void paste_selection(stick_man& canv, const QByteArray& bytes);

}