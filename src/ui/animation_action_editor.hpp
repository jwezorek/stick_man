#pragma once

#include <QtWidgets>
#include <functional>
#include <optional>
#include <vector>

#include "../core/sm_animation.hpp"
#include "widgets/timeline.hpp"
#include "tools/select_tool_panel.hpp"
#include "tools/drag_state.hpp"

namespace ui::canvas { class scene; }

namespace ui::animation_editing {

    struct timeline_presentation {
        QString label;
        timeline_color color = timeline_color::blue;
        bool invalid = false;
    };

    struct authoring_presentation {
        bool moved = false;
        QString preview;
    };

    struct adornment_callbacks {
        std::function<void(sm::action_data)> preview;
        std::function<void(sm::action_data)> commit;
        std::function<void()> cancel;
    };

    struct action_edit_result {
        std::optional<sm::action_data> data;
        QString error;
    };

    // The concrete action alternatives are deliberately dispatched in this module.
    // animation_timeline and rig_interaction consume these generic editor services
    // without knowing which action_data alternative is active.
    timeline_presentation timeline_item_for(const sm::action_data& data,
        const sm::topology& topology, sm::object_id character_root_bone);
    QString selection_text_for(const sm::action_data& data, const sm::topology& topology);
    bool uses_easing(const sm::action_data& data);
    sm::easing authoring_easing(const sm::action_data& data, sm::easing requested);
    QString authoring_begin_message(const sm::action_data& data);
    authoring_presentation authoring_update(const sm::action_data& data);
    QString interactive_preview_message(const sm::action_data& data);
    bool editor_equivalent(const sm::action_data& lhs, const sm::action_data& rhs);

    void install_adornment(canvas::scene& scene, const sm::animation_action& action,
        const sm::action_evaluation_context& context, adornment_callbacks callbacks);

    void sync_translation_tool_properties(tool::select_tool_panel& panel,
        const sm::action_data* selected);
    std::optional<sm::action_data> apply_translation_tool_properties(const sm::action_data& selected,
        tool::animation_translation_settings settings);
    action_edit_result capture_pins(const sm::action_data& selected, const sm::topology& topology,
        const std::vector<sm::object_id>& pinned_nodes);

    // Gesture-to-action construction is action-specific editor behavior too. Keeping
    // it here leaves rig_interaction responsible only for generic drag mechanics.
    sm::action_data authored_action_for(const tool::rotation_state& state);
    std::optional<sm::action_data> authored_action_for(const tool::translation_state& state);

    class action_properties final : public QWidget {
        QComboBox *bone_, *pivot_, *propagation_, *effector_, *pivot_node_;
        QDoubleSpinBox* angle_;
        QLabel *bone_label_, *pivot_label_, *propagation_label_, *effector_label_, *pivot_node_label_, *angle_label_;
        const sm::topology* topology_ = nullptr;
        std::optional<sm::action_data> current_;
        std::function<void(sm::action_data)> edit_;
        bool updating_ = false;

        template<class Action, class Edit>
        void edit_as(Edit&& edit) {
            if (updating_ || !current_) return;
            auto data = *current_;
            if (auto* action = std::get_if<Action>(&data)) {
                edit(*action);
                current_ = data;
                if (edit_) edit_(std::move(data));
            }
        }
        void update_visibility();
    public:
        explicit action_properties(QWidget* parent = nullptr);
        void set_edit_callback(std::function<void(sm::action_data)> callback) { edit_ = std::move(callback); }
        void set_topology(const sm::topology* topology);
        void set_action(const sm::animation_action* action);
        void preview(const sm::action_data& data);
        void focus_primary_editor();
    };
}
