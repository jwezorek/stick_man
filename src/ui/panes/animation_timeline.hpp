#pragma once
#include <QtWidgets>
#include "../../model/project.hpp"
#include "../widgets/timeline.hpp"

namespace ui::canvas { class manager; }
namespace ui::tool { class manager; }
namespace ui::pane {
    // Owns playback time and translates generic timeline intentions to animation commands.
    class animation_timeline : public QDockWidget {
        Q_OBJECT
        mdl::project& project_;
        canvas::manager& canvases_;
        tool::manager& tools_;
        sm::topology* working_ = nullptr;
        sm::object_id character_, animation_, selected_;
        timeline* timeline_;
        QPushButton *play_, *undo_, *redo_, *apply_, *remove_;
        QComboBox *bone_, *pivot_, *propagation_, *effector_, *pivot_node_, *easing_, *layer_;
        QSpinBox *start_, *duration_;
        QDoubleSpinBox* angle_;
        QLabel *time_label_, *status_;
        QLabel *bone_label_, *pivot_label_, *propagation_label_, *effector_label_, *pivot_node_label_, *angle_label_;
        QWidget* parameters_;
        QTimer timer_;
        QElapsedTimer clock_;
        sm::animation_time time_ = 0, playback_start_ = 0;
        row_head_position insertion_;
        bool updating_ = false;
        using authored_rotation = std::variant<sm::rigid_rotation, sm::ik_rotation>;
        struct gesture {
            sm::animation_action action;
            row_head_position row;
            bool moved = false;
        };
        std::optional<gesture> gesture_;
        const sm::animation* current() const;
        const sm::animation_action* selected_action() const;
        void refresh();
        void refresh_parameters();
        void present(const sm::animation& animation, sm::animation_time time,
            std::optional<sm::object_id> provisional = {});
        void evaluate(const sm::animation& animation, sm::animation_time time);
        std::optional<sm::animation> place(sm::animation_action action, row_head_position row,
            bool replace, bool explain = false);
        bool commit(const sm::animation& animation);
        void select_action(QString id);
        void apply_changes();
        void delete_action();
        void move_action(QString id, qint64 start, row_head_position row);
        void resize_action(QString id, qint64 start, qint64 end);
        void rotation_begin(const authored_rotation& rotation);
        void rotation_update(const authored_rotation& rotation);
        void rotation_complete(const authored_rotation& rotation);
        void focus_action_editor(QString id);
        void update_action_field_visibility();
        void tick();
        void message(QString text);
    public:
        animation_timeline(mdl::project& project, canvas::manager& canvases,
            tool::manager& tools, QWidget* parent);
        void begin(sm::object_id character, sm::object_id animation, sm::topology& working);
        void end();
        void seek(sm::animation_time time);
        void play();
        void pause();
        bool playing() const { return timer_.isActive(); }
        sm::animation_time time() const { return time_; }
        void cancel_gesture();
    };
}
