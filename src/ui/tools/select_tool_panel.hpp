#pragma once

#include "tool.hpp"
#include "drag_state.hpp"

/*------------------------------------------------------------------------------------------------*/

namespace ui {

    namespace tool {

        struct sel_drag_settings {
            bool is_in_rotate_mode_;
            bool rotate_on_pinned_;
            sel_drag_mode rotate_mode_;
            sel_drag_mode trans_mode_;
        };

        class select_tool_panel : public QWidget {
            enum class drag_behavior {
                select_only,
                translate,
                rotate
            };

            QPushButton* pin_button_;
            QComboBox* drag_behavior_;
            QLabel* mode_label_;
            QComboBox* mode_;
            QCheckBox* rotate_on_pin_;

            sel_drag_mode rotate_mode_ = sel_drag_mode::rigid;
            sel_drag_mode trans_mode_ = sel_drag_mode::rigid;

            drag_behavior current_drag_behavior() const;
            sel_drag_mode selected_mode() const;
            void populate_mode_combo();
            void update_controls();

        public:
            select_tool_panel();
            void init();
            sel_drag_settings settings() const;
            QPushButton& pin_button() const;
            bool has_drag_behavior() const;

        };

    }
}
