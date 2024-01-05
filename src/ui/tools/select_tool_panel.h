#pragma once

#include "tool.h"

namespace ui {

    namespace tool {

        class select_tool_panel : public QWidget {
            QCheckBox* drag_behaviors_;
            QRadioButton* rotate_;
            QCheckBox* rotate_on_pin_;
            QRadioButton* translate_;
            QCheckBox* trans_selection_;
            QRadioButton* trans_rag_doll_mode_;
            QRadioButton* trans_rubber_band_mode_;
            QRadioButton* trans_rigid_mode_;
            QRadioButton* rot_rag_doll_mode_;
            QRadioButton* rot_rubber_band_mode_;
            QRadioButton* rot_rigid_mode_;
        public:
            select_tool_panel();
        };

    }
}