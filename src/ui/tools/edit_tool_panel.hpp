#pragma once

#include "tool.hpp"
#include "drag_state.hpp"

/*------------------------------------------------------------------------------------------------*/

namespace ui {

    namespace tool {

        struct edit_drag_settings {
            bool is_in_rotate_mode_;
            bool rotate_on_pinned_;
            edit_drag_mode rotate_mode_;
            edit_drag_mode trans_mode_;
        };

        class edit_tool_panel : public QWidget {
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

            edit_drag_mode rotate_mode_ = edit_drag_mode::rigid;
            edit_drag_mode trans_mode_ = edit_drag_mode::rigid;

            drag_behavior current_drag_behavior() const;
            edit_drag_mode selected_mode() const;
            void populate_mode_combo();
            void update_controls();

        public:
            edit_tool_panel();
            void init();
            edit_drag_settings settings() const;
            QPushButton& pin_button() const;
            bool has_drag_behavior() const;

        };

    }
}
