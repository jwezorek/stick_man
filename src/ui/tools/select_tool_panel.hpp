#pragma once

#include "tool.hpp"
#include "drag_state.hpp"
#include "../../core/sm_animation.hpp"
#include <optional>
#include <functional>

/*------------------------------------------------------------------------------------------------*/

namespace ui {

    namespace tool {

        struct sel_drag_settings {
            bool is_in_rotate_mode_;
            bool rotate_on_pinned_;
            sel_drag_mode rotate_mode_;
            sel_drag_mode trans_mode_;
        };

        struct animation_translation_settings {
            sm::motion_path_kind path = sm::motion_path_kind::straight;
            sm::translation_reference reference = sm::translation_reference::character_root;
            sm::object_id reference_bone;
        };
      
        class select_tool_panel : public QWidget {
            QPushButton* pin_button_;
            QCheckBox* drag_behaviors_;
            QRadioButton* rotate_;
            QCheckBox* rotate_on_pin_;
            QRadioButton* rot_rag_doll_mode_;
            QRadioButton* rot_unique_mode_;
            QRadioButton* rot_rigid_mode_;
            QRadioButton* translate_;
            QRadioButton* trans_rag_doll_mode_;
            QRadioButton* trans_rubber_band_mode_;
            QRadioButton* trans_rigid_mode_;

            QGroupBox* animation_translation_group_;
            QComboBox* path_;
            QComboBox* reference_;
            QComboBox* reference_bone_;
            QLabel* reference_bone_label_;
            QPushButton* capture_pins_;
            std::function<void()> animation_property_changed_;
            std::function<void()> capture_pins_requested_;

            QButtonGroup* toplevel_group_;
            QButtonGroup* translate_group_;
            QButtonGroup* rotate_group_;

            std::vector<QWidget*> rot_ctrls(bool include_master);
            std::vector<QWidget*> trans_ctrls(bool include_master);
            sel_drag_mode rot_mode() const;
            sel_drag_mode trans_mode() const;
            void update_reference_bone_enabled();

        public:
            select_tool_panel();
            void init();
            sel_drag_settings settings() const;
            QPushButton& pin_button() const;
            bool has_drag_behavior() const;

            animation_translation_settings animation_translation() const;
            void set_animation_mode(bool enabled);
            void set_reference_bones(const std::vector<std::pair<sm::object_id,std::string>>& bones);
            void set_animation_translation(animation_translation_settings settings, bool ik_selected);
            void set_animation_property_changed(std::function<void()> callback);
            void set_capture_pins_requested(std::function<void()> callback);
        };

    }
}
