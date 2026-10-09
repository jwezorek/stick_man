#pragma once
#include <QWidget>
#include <QtWidgets>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <vector>
#include <memory>
#include <stack>
#include <cstddef>
#include <tuple>
#include <functional>
#include <optional>
#include <unordered_set>
#include "../core/sm_project.hpp"
#include "handle.hpp"
#include "history_state.hpp"

/*------------------------------------------------------------------------------------------------*/

namespace mdl {

    class project;
    enum class animation_display_status { editing, empty, sampled, sampling_failed, reconstruction_failed };
    struct command {
        std::function<sm::result(project&)> redo;
        std::function<void(project&)> undo;
        bool document_edit = true;
        bool animation_edit = false;
        std::optional<sm::object_id> artwork_character;
        bool backgrounds_edit = false;
        history_state::transition history_transition;
    };
    class project : public QObject {

        friend class commands;

        Q_OBJECT

        sm::project core_;
        struct animation_edit_session {
            sm::object_id character;
            sm::object_id animation;
            sm::topology working_topology;
            sm::animation_assets original_animation_data;
            std::optional<sm::object_id> selected_keyframe;
            // The final keyframe has no Core transition yet, but pins still need
            // to participate in authoring/IK there.  Keep the would-be outgoing
            // transition pins in session state until a transition is created.
            std::unordered_set<sm::object_id> terminal_pinned_nodes;
            std::size_t authored_depth = 0;
            std::stack<command> redo_stack;
            std::stack<command> undo_stack;
        };
        std::optional<animation_edit_session> animation_session_;
        std::unique_ptr<sm::topology> playback_topology_;
        std::unordered_set<sm::object_id> playback_pinned_node_ids_;
        sm::constraint_map playback_rotation_constraints_;
        std::optional<sm::object_id> playback_transition_id_;
        animation_display_status playback_status_ = animation_display_status::editing;
        std::optional<sm::result> playback_error_;
        bool show_previous_pose_ = false;
        std::stack<command> redo_stack_;
        std::stack<command> undo_stack_;
        history_state history_;
        std::size_t next_node_name_ = 1;
        std::size_t next_bone_name_ = 1;
        std::optional<std::size_t> animation_display_transition_index() const;
        void clear_redo_stack();
        void clear_session_redo_stack();
        void emit_history_state(bool was_dirty);
        sm::result execute_command(const command& cmd);
        sm::result execute_session_command(const command& cmd);
        void notify_command_change(const command& cmd);
        void rename_aux(handle id, const std::string& new_name);
        sm::topology_change replace_skeletons_aux(
            const std::vector<sm::object_id>& replacees,
            const std::vector<sm::skel_ref>& replacements,
            const std::unordered_set<sm::object_id>& regenerate_ids = {},
            const sm::membership_state* membership = nullptr);
        void clear();
        std::string next_default_node_name();
        std::string next_default_bone_name();
        void advance_default_name_counters_from_topology();
    public:
        project();
        ~project() override;
        // Authoring APIs always use topology()/get(); only rendering uses this view.
        const sm::topology& display_topology() const;
        bool animation_preview_active() const { return bool(playback_topology_); }
        animation_display_status preview_status() const { return playback_status_; }
        std::optional<sm::result> preview_error() const { return playback_error_; }
        animation_display_status preview_animation_time(double seconds);
        void exit_animation_preview();
        bool animation_mode() const { return animation_session_.has_value(); }
        sm::result begin_animation_session(sm::object_id character, sm::object_id animation);
        void end_animation_session();
        std::optional<sm::object_id> animation_session_character() const;
        std::optional<sm::object_id> animation_session_animation() const;
        std::optional<sm::object_id> animation_session_keyframe() const;
        std::unordered_set<sm::object_id> animation_session_pinned_nodes() const;
        struct animation_path_context {
            sm::animation_path path;
            sm::point start, end; // animation-root-local endpoint positions
            sm::animation_root_frame frame;
        };
        bool animation_has_outgoing_transition() const;
        std::vector<sm::object_id> animation_session_path_nodes() const;
        std::optional<animation_path_context> animation_session_path_context(sm::object_id node) const;
        sm::result set_animation_path(sm::object_id node, std::optional<sm::animation_path> path);
        sm::constraint_map animation_session_rotation_constraints() const;
        std::expected<sm::constraint_map, sm::result> animation_edit_constraints() const;
        std::expected<sm::object_id, sm::result> add_animation_rotation_constraint(
            sm::object_id target, sm::rotation_reference reference, sm::angle_range allowed);
        std::optional<sm::rotation_constraint> animation_session_rotation_constraint(sm::object_id id) const;
        sm::result preview_animation_rotation_constraint(sm::object_id id, sm::rotation_constraint definition);
        sm::result update_animation_rotation_constraint(sm::object_id id, sm::rotation_constraint definition);
        sm::result remove_animation_rotation_constraint(sm::object_id id);
        std::unordered_set<sm::object_id> animation_session_incoming_locked_nodes() const;
        sm::constraint_map animation_session_incoming_rotation_constraints() const;
        std::optional<std::string> animation_session_incoming_lock_source_label(sm::object_id node) const;
        sm::result set_animation_transition_duration(sm::object_id transition, double seconds);
        sm::result insert_animation_keyframe(double seconds);
        sm::result set_animation_outgoing_transition_node_pinned(sm::object_id node, bool pinned);
        sm::result select_animation_keyframe(sm::object_id keyframe);
        sm::result add_animation_keyframe();
        sm::result duplicate_animation_keyframe();
        sm::result rename_animation_keyframe(const std::optional<std::string>& name);
        sm::result delete_animation_keyframe();
        bool show_previous_pose() const noexcept { return show_previous_pose_; }
        void set_show_previous_pose(bool show);
        void edit_animation_data(sm::object_id character, const std::function<void(sm::animation_assets&)>& edit);
        void apply_pose(sm::object_id character, sm::object_id pose);
        const sm::project& core() const;
        sm::project& core();
        // Validate before recording the undoable edit.
        void edit_artwork(const sm::object_id& character, const std::function<void(sm::artwork&)>& edit);
        void edit_backgrounds(const std::function<void(std::vector<sm::background_image>&)>& edit);
        const sm::topology& topology() const;
        model_object get(const sm::object_id& id);
        const_model_object get(const sm::object_id& id) const;
        bool can_undo() const;
        bool can_redo() const;
        bool is_dirty() const noexcept;
        void mark_saved();
        void new_document();
        std::expected<sm::project_buffer, sm::project_result> serialize() const;
        static sm::project_result validate_serialized(std::span<const std::uint8_t> buffer);
        sm::project_result deserialize_result(std::span<const std::uint8_t> buffer);
        bool deserialize(std::span<const std::uint8_t> buffer);
        void undo();
        sm::result redo();
        sm::result add_bone(const handle& node_u, const handle& node_v);
        sm::result adopt_skeletons(const sm::object_id& character_id,
            std::span<const sm::const_skel_ref> skeletons);
        std::expected<sm::object_id, sm::result> make_character(std::span<const sm::const_skel_ref> skeletons);
        std::expected<sm::object_id, sm::result> paste_character(const sm::topology& rig, const std::string& name,
            const sm::artwork& artwork = {}, const sm::animation_assets& animation_data = {});
        sm::result delete_character(const sm::object_id& id);
        std::expected<sm::object_id, sm::result> add_rotation_constraint(
            sm::object_id target, sm::rotation_reference reference, sm::angle_range allowed,
            std::string name = "Rotation constraint");
        std::expected<sm::object_id, sm::result> add_rigid_triangle_constraint(
            sm::object_id first, sm::object_id second, std::string name = "Rigid triangle");
        sm::result update_constraint(sm::object_id id, sm::constraint_definition definition);
        sm::result remove_constraint(sm::object_id id);
        void record_transient_edit(std::function<void()> redo, std::function<void()> undo);
        bool rename(const sm::object_id& id, const std::string& new_name);
        sm::result set_character_root_bone(sm::object_id character_id, sm::object_id bone_id);
        void add_new_skeleton_root(sm::point loc);
        bool rename(skel_piece piece, const std::string& new_name);
        sm::result replace_skeletons(
            const std::vector<sm::object_id>& replacees,
            const std::vector<sm::skel_ref>& replacements,
            const std::unordered_set<sm::object_id>& regenerate_ids = {}
        );
        void transform(const std::vector<handle>& nodes,
            const std::function<void(sm::node&)>& fn);
        void transform(const std::vector<handle>& nodes,
            const std::function<void(sm::bone&)>& fn);
        using node_locs = std::vector<std::tuple<handle, sm::point>>;
        void transform_node_positions(
            const node_locs& old_locs, const node_locs& new_locs
        );
    signals:
        void animation_editing_requested();
        void animation_display_changing(bool preview);
        void animation_display_changed();
        void animation_display_status_changed();
        void topology_about_to_reset();
        void animation_session_ending();
        void model_about_to_be_destroyed();
        void pre_new_bone_added(sm::node& u, sm::node& v);
        void new_bone_added(sm::bone& bone);
        void new_project_opened(project& model);
        void new_skeleton_added(sm::skel_ref skel);
        void refresh_canvas(project& model, bool clear);
        void animation_keyframe_selected(sm::object_id keyframe);
        void animation_preview_changed();
        void animation_authoring_error(const QString& message);
        void name_changed(const_skel_piece piece, const std::string& new_name);
        void project_changed(project& model);
        void artwork_changed(project& model, sm::object_id character);
        void backgrounds_changed(project& model);
        void select_character(sm::object_id id);
        void refresh_undo_redo_state(bool, bool);
        void dirty_changed(bool dirty);
    };
}
