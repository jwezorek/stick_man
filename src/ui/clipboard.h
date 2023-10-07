#pragma once

/*------------------------------------------------------------------------------------------------*/

namespace ui {

    class stick_man;

    namespace clipboard {
        void cut(stick_man& main_wnd);
        void copy(stick_man& main_wnd);
        void paste(stick_man& main_wnd);
        void del(stick_man& main_wnd);
    }

}