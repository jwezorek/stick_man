#pragma once

#include <QtWidgets>

/*------------------------------------------------------------------------------------------------*/

namespace ui {

    class canvas;

    QByteArray cut_selection(canvas& canv);
    QByteArray copy_selection(canvas& canv);
    void paste_selection(canvas& canv, const QByteArray& bytes);

}