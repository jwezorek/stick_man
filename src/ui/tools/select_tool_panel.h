#pragma once

#include "tool.h"
#include <optional>

namespace ui {

    namespace tool {

        enum class sel_drag_mode {
            rag_doll,
            rubber_band,
            rigid
        };

        struct sel_drag_settings {
            bool is_in_rotate_mode_;
            bool rotate_on_pin_;
            sel_drag_mode rotate_mode_;
            bool trans_sel_;
            sel_drag_mode trans_mode_;
        };
      

        class select_tool_panel : public QWidget {

            QCheckBox* drag_behaviors_;
            QRadioButton* rotate_;
            QCheckBox* rotate_on_pin_;
            QRadioButton* rot_rag_doll_mode_;
            QRadioButton* rot_rubber_band_mode_;
            QRadioButton* rot_rigid_mode_;
            QRadioButton* translate_;
            QCheckBox* trans_selection_;
            QRadioButton* trans_rag_doll_mode_;
            QRadioButton* trans_rubber_band_mode_;
            QRadioButton* trans_rigid_mode_;

            QButtonGroup* toplevel_group_;
            QButtonGroup* translate_group_;
            QButtonGroup* rotate_group_;

            std::vector<QWidget*> rot_ctrls(bool include_master);
            std::vector<QWidget*> trans_ctrls(bool include_master);
            sel_drag_mode rot_mode() const;
            sel_drag_mode trans_mode() const;

        public:
            select_tool_panel();
            void init();
            std::optional<sel_drag_settings> settings() const;
        };

    }
}