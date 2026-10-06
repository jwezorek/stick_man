#include "project.hpp"
#include "commands.hpp"
#include "../core/sm_project.hpp"
#include "../core/sm_geometry_batch.hpp"
#include <charconv>
#include <algorithm>
#include <system_error>
#include <type_traits>
#include <string_view>
#include <stdexcept>
#include <unordered_set>
#include <unordered_map>
#include <utility>
#include <cmath>

/*------------------------------------------------------------------------------------------------*/
namespace {
    std::size_t default_name_index(std::string_view name, std::string_view prefix) {
        if (!name.starts_with(prefix)) {
            return 0;
        }
        auto suffix = name.substr(prefix.size());
        if (suffix.empty()) {
            return 0;
        }
        std::size_t index = 0;
        const char* first = suffix.data();
        const char* last = first + suffix.size();
        auto [ptr, ec] = std::from_chars(first, last, index);
        if (ec != std::errc{} || ptr != last || index == 0) {
            return 0;
        }
        return index;
    }


    constexpr double k_animation_pin_position_tolerance = 1e-8;

    std::optional<sm::point> pose_node_position(const sm::skeletal_pose& pose,
            const sm::topology& reference, std::span<const sm::object_id> skeletons, sm::object_id node_id) {
        sm::topology copy;
        try {
            for (auto sid : skeletons) {
                auto skel = reference.skeleton(sid);
                if (!skel || !skel->get().copy_to(copy))
                    return {};
            }
            sm::apply_skeletal_pose(pose, copy, skeletons);
            auto node = copy.get<sm::node>(node_id);
            if (!node)
                return {};
            return node->get().world_pos();
        } catch (...) {
            return {};
        }
    }

    sm::result validate_pin_endpoints(const sm::animation& animation,
            const sm::topology& reference, std::span<const sm::object_id> skeletons) {
        for (std::size_t i = 0; i + 1 < animation.keyframes.size(); ++i) {
            const auto& from = animation.keyframes[i];
            const auto& to = animation.keyframes[i + 1];
            for (auto node_id : from.pinned_nodes) {
                auto a = pose_node_position(from.pose, reference, skeletons, node_id);
                auto b = pose_node_position(to.pose, reference, skeletons, node_id);
                if (!a || !b || sm::distance(*a, *b) > k_animation_pin_position_tolerance)
                    return sm::result::invalid_animation;
            }
        }
        return sm::result::success;
    }
    bool same_constraint_definition(const sm::constraint_definition& a,
            const sm::constraint_definition& b) {
        if (a.index() != b.index())
            return false;
        if (auto ar = std::get_if<sm::rotation_constraint>(&a)) {
            const auto& br = std::get<sm::rotation_constraint>(b);
            return ar->target_bone == br.target_bone && ar->reference == br.reference &&
                ar->allowed.start_angle == br.allowed.start_angle &&
                ar->allowed.span_angle == br.allowed.span_angle;
        }
        const auto& at = std::get<sm::rigid_triangle_constraint>(a);
        const auto& bt = std::get<sm::rigid_triangle_constraint>(b);
        return at.first_bone == bt.first_bone && at.second_bone == bt.second_bone &&
            at.relative_angle == bt.relative_angle;
    }
}
/*------------------------------------------------------------------------------------------------*/
void mdl::project::clear_redo_stack() { redo_stack_ = {}; }
void mdl::project::clear_session_redo_stack() {
    if (animation_session_) animation_session_->redo_stack = {};
}
void mdl::project::emit_history_state(bool was_dirty) {
    emit refresh_undo_redo_state(can_redo(), can_undo());
    const bool dirty = is_dirty();
    if (dirty != was_dirty) emit dirty_changed(dirty);
}
void mdl::project::notify_command_change(const command& cmd) {
    if (cmd.artwork_character)
        emit artwork_changed(*this, *cmd.artwork_character);
    else if (cmd.backgrounds_edit)
        emit backgrounds_changed(*this);
    else
        emit project_changed(*this);
}
sm::result mdl::project::execute_command(const command& cmd) {
    if (animation_session_)
        return sm::result::invalid_membership;
    const bool was_dirty = is_dirty();
    command stored = cmd;
    stored.redo(*this);
    if (stored.outcome && stored.outcome() != sm::result::success)
        return stored.outcome();
    clear_redo_stack();
    if (stored.document_edit)
        stored.history_transition = history_.advance();
    undo_stack_.push(stored);
    emit_history_state(was_dirty);
    notify_command_change(stored);
    return sm::result::success;
}
sm::result mdl::project::execute_session_command(const command& cmd) {
    if (!animation_session_)
        return sm::result::invalid_membership;
    exit_animation_preview();
    const bool was_dirty = is_dirty();
    command stored = cmd;
    stored.document_edit = false;
    stored.redo(*this);
    if (stored.outcome && stored.outcome() != sm::result::success)
        return stored.outcome();
    clear_session_redo_stack();
    if (stored.animation_edit) ++animation_session_->authored_depth;
    animation_session_->undo_stack.push(std::move(stored));
    emit refresh_undo_redo_state(can_redo(), can_undo());
    if (was_dirty != is_dirty()) emit dirty_changed(is_dirty());
    return sm::result::success;
}

mdl::project::project() {
    // These are authoring notifications. Rendering-only refresh_canvas never
    // invalidates a held sample or restarts the clock.
    connect(this, &project::animation_preview_changed, this, &project::exit_animation_preview);
    connect(this, &project::project_changed, this, [this] {
        if (animation_preview_active())
            exit_animation_preview();
    });
}
mdl::project::~project() {
    emit animation_session_ending();
    emit topology_about_to_reset();
    emit model_about_to_be_destroyed();
}

const sm::project& mdl::project::core() const { return core_; }
sm::project& mdl::project::core() { return core_; }
void mdl::project::edit_artwork(const sm::object_id& id, const std::function<void(sm::artwork&)>& edit) {
    if (animation_mode())
        return;
    auto before = core_.artwork(id);
    auto after = before;
    edit(after);
    command cmd{
        [id, after](project& p) { p.core_.artwork(id) = after; },
        [id, before](project& p) { p.core_.artwork(id) = before; }
    };
    cmd.artwork_character = id;
    execute_command(cmd);
}
void mdl::project::edit_backgrounds(
        const std::function<void(std::vector<sm::background_image>&)>& edit) {
    if (animation_mode())
        return;
    auto before = core_.backgrounds();
    auto after = before;
    edit(after);
    // Validate atomically before placing the edit on the history stack. Image
    // resources share immutable backing, so these snapshots remain inexpensive.
    core_.set_backgrounds(after);
    core_.set_backgrounds(before);
    command cmd{
        [after](project& p) { p.core_.set_backgrounds(after); },
        [before](project& p) { p.core_.set_backgrounds(before); }
    };
    cmd.backgrounds_edit = true;
    execute_command(cmd);
}
const sm::topology& mdl::project::topology() const {
    return animation_session_ ? animation_session_->working_topology : core_.topology();
}

mdl::model_object mdl::project::get(const sm::object_id& id) {
    if (animation_session_) {
        if (auto node = animation_session_->working_topology.get<sm::node>(id))
            return *node;
        if (auto bone = animation_session_->working_topology.get<sm::bone>(id))
            return *bone;
        throw std::runtime_error("animation session object ID not found");
    }
    return core_.get(id);
}

mdl::const_model_object mdl::project::get(const sm::object_id& id) const {
    if (animation_session_) {
        const auto& working = std::as_const(animation_session_->working_topology);
        if (auto node = working.get<sm::node>(id))
            return sm::const_node_ref(std::as_const(node->get()));
        if (auto bone = working.get<sm::bone>(id))
            return sm::const_bone_ref(std::as_const(bone->get()));
        if (auto skel = working.skeleton(id))
            return *skel;
        if (auto it = working.constraints().find(id); it != working.constraints().end())
            return sm::const_constraint_ref(std::as_const(it->second));
    }
    return core_.get(id);
}

void mdl::project::clear() {
    exit_animation_preview();
    emit animation_session_ending();
    emit topology_about_to_reset();
    animation_session_.reset();
    core_.clear();
    redo_stack_ = {};
    undo_stack_ = {};
    history_.reset_clean();
    next_node_name_ = 1;
    next_bone_name_ = 1;
}
std::string mdl::project::next_default_node_name() {
    return "node-" + std::to_string(next_node_name_++);
}

std::string mdl::project::next_default_bone_name() {
    return "bone-" + std::to_string(next_bone_name_++);
}
void mdl::project::advance_default_name_counters_from_topology() {
    const auto& topology = std::as_const(core_).topology();
    for (auto skel : topology.skeletons()) {
        for (auto node : skel->nodes()) {
            auto index = default_name_index(node->name(), "node-");
            if (index != 0) {
                next_node_name_ = std::max(next_node_name_, index + 1);
            }
        }
        for (auto bone : skel->bones()) {
            auto index = default_name_index(bone->name(), "bone-");
            if (index != 0) {
                next_bone_name_ = std::max(next_bone_name_, index + 1);
            }
        }
    }
}
void mdl::project::undo() {
    exit_animation_preview();
    if (!can_undo())
        return;
    if (animation_session_) {
        const bool was_dirty = is_dirty();
        auto cmd = animation_session_->undo_stack.top();
        animation_session_->undo_stack.pop();
        cmd.undo(*this);
        if (cmd.animation_edit && animation_session_->authored_depth > 0) --animation_session_->authored_depth;
        animation_session_->redo_stack.push(std::move(cmd));
        emit refresh_undo_redo_state(can_redo(), can_undo());
        if (was_dirty != is_dirty()) emit dirty_changed(is_dirty());
        return;
    }
    const bool was_dirty = is_dirty();
    auto cmd = undo_stack_.top();
    undo_stack_.pop();
    cmd.undo(*this);
    if (cmd.document_edit)
        history_.undo(cmd.history_transition);
    redo_stack_.push(cmd);
    emit_history_state(was_dirty);
    notify_command_change(cmd);
}
sm::result mdl::project::redo() {
    exit_animation_preview();
    if (!can_redo())
        return sm::result::success;
    if (animation_session_) {
        const bool was_dirty = is_dirty();
        auto cmd = animation_session_->redo_stack.top();
        cmd.redo(*this);
        if (cmd.outcome && cmd.outcome() != sm::result::success)
            return cmd.outcome();
        animation_session_->redo_stack.pop();
        if (cmd.animation_edit) ++animation_session_->authored_depth;
        animation_session_->undo_stack.push(std::move(cmd));
        emit refresh_undo_redo_state(can_redo(), can_undo());
        if (was_dirty != is_dirty()) emit dirty_changed(is_dirty());
        return sm::result::success;
    }
    const bool was_dirty = is_dirty();
    auto cmd = redo_stack_.top();
    cmd.redo(*this);
    if (cmd.outcome && cmd.outcome() != sm::result::success)
        return cmd.outcome();
    redo_stack_.pop();
    if (cmd.document_edit)
        history_.redo(cmd.history_transition);
    undo_stack_.push(cmd);
    emit_history_state(was_dirty);
    notify_command_change(cmd);
    return sm::result::success;
}
bool mdl::project::can_undo() const {
    return animation_session_ ? !animation_session_->undo_stack.empty() : !undo_stack_.empty();
}
bool mdl::project::can_redo() const {
    return animation_session_ ? !animation_session_->redo_stack.empty() : !redo_stack_.empty();
}
bool mdl::project::is_dirty() const noexcept {
    return history_.dirty() || (animation_session_ && animation_session_->authored_depth != 0);
}
void mdl::project::mark_saved() {
    const bool was_dirty = is_dirty();
    history_.mark_saved();
    if (was_dirty != is_dirty()) emit dirty_changed(is_dirty());
}
void mdl::project::new_document() {
    const bool was_dirty = is_dirty();
    clear();
    emit_history_state(was_dirty);
    emit new_project_opened(*this);
}
std::expected<sm::project_buffer, sm::project_result> mdl::project::serialize() const {
    return core_.serialize();
}
sm::project_result mdl::project::validate_serialized(std::span<const std::uint8_t> buffer) {
    sm::project candidate;
    return candidate.deserialize(buffer);
}
sm::project_result mdl::project::deserialize_result(std::span<const std::uint8_t> buffer) {
    // Validate before detaching the live view; malformed input leaves it intact.
    const auto validation = validate_serialized(buffer);
    if (validation != sm::project_result::success)
        return validation;
    exit_animation_preview();
    emit animation_session_ending();
    emit topology_about_to_reset();
    const bool was_dirty = is_dirty();
    // Core deserialization is transactional: it builds a staged project and only
    // commits to core_ after the entire package has validated successfully.
    const auto result = core_.deserialize(buffer);
    if (result != sm::project_result::success) {
        return result;
    }
    redo_stack_ = {};
    undo_stack_ = {};
    history_.reset_clean();
    animation_session_.reset();
    next_node_name_ = 1;
    next_bone_name_ = 1;
    advance_default_name_counters_from_topology();
    emit_history_state(was_dirty);
    emit new_project_opened(*this);
    return sm::project_result::success;
}
bool mdl::project::deserialize(std::span<const std::uint8_t> buffer) {
    if (animation_mode())
        return false;
    return deserialize_result(buffer) == sm::project_result::success;
}
sm::result mdl::project::add_bone(const handle& u, const handle& v) {
    auto& node_u = commands::resolve<sm::node>(*this, u);
    auto& node_v = commands::resolve<sm::node>(*this, v);
    auto preview = core_.preview_create_bone(node_u, node_v);
    if (!preview)
        return preview.error();
    return execute_command(commands::make_add_bone_command(
        u, v, next_default_bone_name(), *preview));
}
sm::result mdl::project::adopt_skeletons(const sm::object_id& character_id,
        std::span<const sm::const_skel_ref> skeletons) {
    struct adoption_state {
        sm::membership_state before;
        std::optional<sm::membership_state> after;
        sm::result status = sm::result::success;
    };
    auto state = std::make_shared<adoption_state>();
    // References are used only during initial execution; redo resolves stable IDs.
    std::vector<sm::const_skel_ref> candidates(skeletons.begin(), skeletons.end());
    return execute_command({
        [state, character_id, candidates](project& proj) {
            if (state->after) {
                state->status = proj.core_.restore_membership(*state->after);
            } else {
                std::vector<sm::object_id> ids;
                for (auto s : candidates)
                    ids.push_back(s->id());
                state->before = proj.core_.snapshot_membership(ids,
                    std::span<const sm::object_id>(&character_id, 1));
                state->status = proj.core_.adopt_skeletons(character_id, candidates);
                if (state->status == sm::result::success)
                    state->after = proj.core_.snapshot_membership(ids);
            }
            if (state->status == sm::result::success) emit proj.refresh_canvas(proj, true);
        },
        [state](project& proj) {
            if (proj.core_.restore_membership(state->before) != sm::result::success)
                throw std::runtime_error("unable to restore adoption membership");
            emit proj.refresh_canvas(proj, true);
        },
        [state] { return state->status; }
    });
}
void mdl::project::add_new_skeleton_root(sm::point loc) {
    if (animation_mode())
        return;
    execute_command(commands::make_create_node_command(loc, next_default_node_name()));
}
std::expected<sm::object_id, sm::result> mdl::project::make_character(
        std::span<const sm::const_skel_ref> skeletons) {
    struct state_type {
        sm::membership_state before, after;
        sm::object_id id;
        sm::result status = sm::result::success;
        bool created = false;
    };
    auto state = std::make_shared<state_type>();
    std::vector<sm::const_skel_ref> candidates(skeletons.begin(), skeletons.end());
    auto result = execute_command({
        [state, candidates](project& proj) {
            if (state->created) {
                state->status = proj.core_.restore_membership(state->after);
            } else {
                std::vector<sm::object_id> ids;
                for (auto s : candidates)
                    ids.push_back(s->id());
                state->before = proj.core_.snapshot_membership(ids);
                auto created = proj.core_.create_character(candidates);
                if (!created) {
                    state->status = created.error();
                    return;
                }
                state->id = created->get().id();
                state->after = proj.core_.snapshot_membership(ids);
                state->created = true;
            }
            if (state->status == sm::result::success) {
                emit proj.refresh_canvas(proj, true);
                emit proj.select_character(state->id);
            }
        },
        [state](project& proj) {
            if (proj.core_.restore_membership(state->before) != sm::result::success)
                throw std::runtime_error("unable to restore character creation");
            emit proj.refresh_canvas(proj, true);
        },
        [state] { return state->status; }
    });
    if (result != sm::result::success)
        return std::unexpected(result);
    return state->id;
}

std::expected<sm::object_id, sm::result> mdl::project::paste_character(
        const sm::topology& rig, const std::string& name, const sm::artwork& artwork,
        const sm::animation_assets& animation_data) {
    if (rig.empty())
        return std::unexpected(sm::result::empty_character);
    struct state_type {
        sm::topology topology;
        sm::membership_state membership;
        std::vector<sm::object_id> ids;
        sm::object_id character = sm::object_id::generate();
        sm::result status = sm::result::success;
    };
    auto state = std::make_shared<state_type>();
    std::unordered_set<std::string> existing_names;
    for (auto character : core_.characters())
        existing_names.insert(character->name());
    auto copied_name = name + " copy";
    for (std::size_t suffix = 2; existing_names.contains(copied_name); ++suffix)
        copied_name = name + " copy " + std::to_string(suffix);
    std::unordered_map<sm::object_id, sm::object_id> remap;
    for (auto skel : rig.skeletons()) {
        remap.emplace(skel->id(), sm::object_id::generate());
        for (auto node : skel->nodes())
            remap.emplace(node->id(), sm::object_id::generate());
        for (auto bone : skel->bones())
            remap.emplace(bone->id(), sm::object_id::generate());
    }
    for (const auto& [id, constraint] : rig.constraints())
        remap.emplace(id, sm::object_id::generate());
    auto copied_artwork = artwork;
    copied_artwork.remap_bones(remap);
    auto copied_animation_data = animation_data;
    sm::remap_animation_assets(copied_animation_data, remap);
    state->membership.characters.push_back({state->character, copied_name,
        std::move(copied_artwork), std::move(copied_animation_data)});
    for (auto skel : rig.skeletons()) {
        auto copy = skel->copy_to(state->topology, remap);
        if (!copy)
            return std::unexpected(copy.error());
        state->ids.push_back(copy->get().id());
        state->membership.parents.emplace(copy->get().id(), state->character);
    }
    auto result = execute_command({
        [state](project& proj) {
            auto change = proj.replace_skeletons_aux({},
                state->topology.skeletons() | std::ranges::to<std::vector<sm::skel_ref>>(),
                {}, &state->membership);
            state->status = change.status;
            if (state->status == sm::result::success) {
                state->ids = std::move(change.added_skeleton_ids);
                state->membership = proj.core_.snapshot_membership(state->ids);
                sm::topology inserted;
                for (const auto& id : state->ids)
                    if (!proj.topology().skeleton(id)->get().copy_to(inserted))
                        throw std::runtime_error("unable to snapshot pasted character");
                state->topology = std::move(inserted);
                emit proj.select_character(state->character);
            }
        },
        [state](project& proj) { proj.replace_skeletons_aux(state->ids, {}); },
        [state] { return state->status; }
    });
    if (result != sm::result::success)
        return std::unexpected(result);
    return state->character;
}

sm::result mdl::project::delete_character(const sm::object_id& id) {
    auto character = core_.character(id);
    if (!character)
        return character.error();
    // Stage 2 replacement is the semantic structural deletion operation: it prunes
    // the empty character and snapshots both identity and membership for undo.
    return replace_skeletons(character->get().rig().skeleton_ids(), {});
}

std::expected<sm::object_id, sm::result> mdl::project::add_rotation_constraint(
        sm::object_id target, sm::rotation_reference reference, sm::angle_range allowed,
        std::string name) {
    for (;;) {
        const sm::object_id id = sm::object_id::generate();
        sm::constraint value{id, name, sm::rotation_constraint{target, reference, allowed}};
        struct state_type { sm::result status = sm::result::success; };
        auto state = std::make_shared<state_type>();
        auto result = execute_command({
            [state, value](project& proj) {
                auto added = proj.core_.add_constraint(value);
                state->status = added ? sm::result::success : added.error();
            },
            [id](project& proj) {
                if (proj.core_.remove_constraint(id) != sm::result::success)
                    throw std::runtime_error("unable to undo rotation constraint creation");
            },
            [state] { return state->status; }
        });
        if (result == sm::result::duplicate_id)
            continue;
        if (result != sm::result::success)
            return std::unexpected(result);
        return id;
    }
}

std::expected<sm::object_id, sm::result> mdl::project::add_rigid_triangle_constraint(
        sm::object_id first, sm::object_id second, std::string name) {
    struct state_type {
        sm::result status = sm::result::success;
        std::optional<sm::constraint> value;
    };
    auto state = std::make_shared<state_type>();
    auto result = execute_command({
        [state, first, second, name = std::move(name)](project& proj) {
            if (state->value) {
                auto restored = proj.core_.add_constraint(*state->value);
                state->status = restored ? sm::result::success : restored.error();
                return;
            }

            // Let Core construct the canonical rigid-triangle definition, including
            // the signed rest angle and all structural validation.  The editor model
            // snapshots the resulting first-class object only so redo can preserve
            // its persistent identity.
            auto added = proj.core_.add_rigid_triangle_constraint(first, second, name);
            if (!added) {
                state->status = added.error();
                return;
            }
            state->value = added->get();
            state->status = sm::result::success;
        },
        [state](project& proj) {
            if (!state->value || proj.core_.remove_constraint(state->value->id()) != sm::result::success)
                throw std::runtime_error("unable to undo rigid triangle creation");
        },
        [state] { return state->status; }
    });
    if (result != sm::result::success)
        return std::unexpected(result);
    return state->value->id();
}

sm::result mdl::project::update_constraint(sm::object_id id, sm::constraint_definition definition) {
    auto current = core_.constraint_by_id(id);
    if (!current)
        return current.error();
    const auto before = current->get().definition();
    if (same_constraint_definition(before, definition))
        return sm::result::success;
    struct state_type { sm::result status = sm::result::success; };
    auto state = std::make_shared<state_type>();
    return execute_command({
        [state, id, definition](project& proj) {
            state->status = proj.core_.update_constraint(id, definition);
        },
        [id, before](project& proj) {
            if (proj.core_.update_constraint(id, before) != sm::result::success)
                throw std::runtime_error("unable to restore constraint definition");
        },
        [state] { return state->status; }
    });
}

sm::result mdl::project::remove_constraint(sm::object_id id) {
    auto current = core_.constraint_by_id(id);
    if (!current)
        return current.error();
    const sm::constraint snapshot = current->get();
    struct state_type { sm::result status = sm::result::success; };
    auto state = std::make_shared<state_type>();
    return execute_command(
        { [state, id](project& proj) { state->status = proj.core_.remove_constraint(id); },
            [snapshot](project& proj) {
                auto restored = proj.core_.add_constraint(snapshot);
                if (!restored)
                    throw std::runtime_error("unable to restore deleted constraint");
            },
            [state] { return state->status; } });
}

void mdl::project::record_transient_edit(std::function<void()> redo, std::function<void()> undo) {
    command cmd{
        [redo = std::move(redo)](project&) { redo(); },
        [undo = std::move(undo)](project&) { undo(); }
    };
    cmd.document_edit = false;
    if (animation_session_)
        execute_session_command(cmd);
    else execute_command(cmd);
}

bool mdl::project::rename(const sm::object_id& id, const std::string& new_name) {
    auto old_name = std::visit([](auto ref) { return ref->name(); }, std::as_const(core_).get(id));
    if (old_name == new_name)
        return true;
    execute_command({
        [id, new_name](project& proj) { proj.rename_aux(id, new_name); },
        [id, old_name](project& proj) { proj.rename_aux(id, old_name); }
    });
    return true;
}
void mdl::project::rename_aux(handle id, const std::string& new_name) {
    core_.rename(id, new_name);
    advance_default_name_counters_from_topology();
    // Existing topology properties use this narrow notification. Character labels
    // and panes resynchronize through the command's project_changed notification.
    std::visit([this, &new_name](auto ref) {
        using value_type = std::remove_cvref_t<decltype(ref.get())>;
        if constexpr (std::is_same_v<value_type, sm::node> ||
                std::is_same_v<value_type, sm::bone> || std::is_same_v<value_type, sm::skeleton>) {
            emit name_changed(const_skel_piece{ref}, new_name);
        }
    }, std::as_const(core_).get(id));
}
bool mdl::project::can_rename(skel_piece, const std::string&) {
    return true;
}
bool mdl::project::rename(skel_piece piece, const std::string& new_name) {
    return rename(to_handle(piece), new_name);
}
void mdl::project::transform(const std::vector<handle>& nodes,
        const std::function<void(sm::node&)>& fn) {
    if (animation_preview_active())
        return;
    if (!animation_session_ || !animation_session_->selected_keyframe) {
        auto cmd = commands::make_transform_bones_or_nodes_command(*this, nodes, {}, fn, {});
        if (animation_session_)
            execute_session_command(cmd);
        else
            execute_command(cmd);
        return;
    }

    // Animation poses store geometry, not an independent mutable rig.  Run the
    // proposed property edit transactionally against the session topology, turn
    // the result into one pose edit, then restore the topology before committing.
    node_locs before, after;
    for (auto skel : topology().skeletons())
        for (auto node : skel->nodes())
            before.emplace_back(node->id(), node->world_pos());
    {
        sm::geometry_batch batch(topology());
        for (auto h : nodes)
            fn(commands::resolve<sm::node>(*this, h));
        if (batch.commit() != sm::result::success)
            return;
    }
    for (auto skel : topology().skeletons())
        for (auto node : skel->nodes())
            after.emplace_back(node->id(), node->world_pos());
    {
        sm::geometry_batch batch(topology());
        for (const auto& [h, pt] : before) commands::resolve<sm::node>(*this, h).set_world_pos(pt);
        if (batch.commit() != sm::result::success)
            throw std::runtime_error("unable to restore animation edit probe");
    }
    transform_node_positions(before, after);
}
void mdl::project::transform(const std::vector<handle>& bones,
        const std::function<void(sm::bone&)>& fn) {
    if (animation_preview_active())
        return;
    if (!animation_session_ || !animation_session_->selected_keyframe) {
        auto cmd = commands::make_transform_bones_or_nodes_command(*this, {}, bones, {}, fn);
        if (animation_session_)
            execute_session_command(cmd);
        else
            execute_command(cmd);
        return;
    }
    node_locs before, after;
    for (auto skel : topology().skeletons())
        for (auto node : skel->nodes())
            before.emplace_back(node->id(), node->world_pos());
    {
        sm::geometry_batch batch(topology());
        for (auto h : bones)
            fn(commands::resolve<sm::bone>(*this, h));
        if (batch.commit() != sm::result::success)
            return;
    }
    for (auto skel : topology().skeletons())
        for (auto node : skel->nodes())
            after.emplace_back(node->id(), node->world_pos());
    {
        sm::geometry_batch batch(topology());
        for (const auto& [h, pt] : before) commands::resolve<sm::node>(*this, h).set_world_pos(pt);
        if (batch.commit() != sm::result::success)
            throw std::runtime_error("unable to restore animation edit probe");
    }
    transform_node_positions(before, after);
}
void mdl::project::transform_node_positions(
        const node_locs& old_locs, const node_locs& new_locs) {
    if (animation_preview_active())
        return;
    if (!animation_session_ || !animation_session_->selected_keyframe) {
        auto cmd = commands::make_transform_node_positions_command(*this, old_locs, new_locs);
        if (animation_session_)
            execute_session_command(cmd);
        else
            execute_command(cmd);
        return;
    }

    const auto character_id = animation_session_->character;
    const auto animation_id = animation_session_->animation;
    const auto keyframe_id = *animation_session_->selected_keyframe;
    const auto skeletons = core_.character(character_id)->get().rig().skeleton_ids();
    auto* animation = core_.animation_data(character_id).find_animation(animation_id);
    auto* keyframe = animation ? animation->find_keyframe(keyframe_id) : nullptr;
    if (!keyframe)
        throw std::runtime_error("selected animation keyframe is missing");
    const auto before_pose = keyframe->pose;

    struct pose_edit_state {
        std::optional<sm::skeletal_pose> after_pose;
        sm::result status = sm::result::success;
    };
    auto state = std::make_shared<pose_edit_state>();
    command cmd;
    cmd.redo = [character_id, animation_id, keyframe_id, new_locs,
            skeletons, before_pose, state](project& proj) {
        auto* animation = proj.core_.animation_data(character_id).find_animation(animation_id);
        auto* keyframe = animation ? animation->find_keyframe(keyframe_id) : nullptr;
        if (!keyframe)
            throw std::runtime_error("animation keyframe missing during pose edit");
        state->status = sm::result::success;

        if (!state->after_pose) {
            sm::geometry_batch batch(proj.topology());
            for (const auto& [node_hnd, loc] : new_locs)
                commands::resolve<sm::node>(proj, node_hnd).set_world_pos(loc);
            if (batch.commit() != sm::result::success) {
                state->status = sm::result::invalid_animation;
                sm::apply_skeletal_pose(before_pose, proj.topology(), skeletons);
                emit proj.animation_authoring_error(QStringLiteral("This edit would violate the current animation pose constraints."));
                emit proj.refresh_canvas(proj, false);
                return;
            }
            auto candidate_pose = sm::capture_skeletal_pose(proj.topology(), skeletons);
            auto candidate_animation = *animation;
            auto* candidate_key = candidate_animation.find_keyframe(keyframe_id);
            candidate_key->pose = candidate_pose;
            if (validate_pin_endpoints(candidate_animation, proj.core_.topology(), skeletons)
                    != sm::result::success) {
                state->status = sm::result::invalid_animation;
                sm::apply_skeletal_pose(before_pose, proj.topology(), skeletons);
                emit proj.animation_authoring_error(QStringLiteral("This edit would violate the current animation pose constraints."));
                emit proj.refresh_canvas(proj, false);
                return;
            }
            state->after_pose = std::move(candidate_pose);
        } else {
            sm::apply_skeletal_pose(*state->after_pose, proj.topology(), skeletons);
        }

        keyframe->pose = *state->after_pose;
        proj.animation_session_->selected_keyframe = keyframe_id;
        emit proj.animation_keyframe_selected(keyframe_id);
        emit proj.animation_preview_changed();
        emit proj.refresh_canvas(proj, false);
    };
    cmd.animation_edit = true;
    cmd.undo = [character_id, animation_id, keyframe_id,
            before_pose, skeletons](project& proj) {
        auto* animation = proj.core_.animation_data(character_id).find_animation(animation_id);
        auto* keyframe = animation ? animation->find_keyframe(keyframe_id) : nullptr;
        if (!keyframe)
            throw std::runtime_error("animation keyframe missing during pose undo");

        keyframe->pose = before_pose;
        sm::apply_skeletal_pose(before_pose, proj.topology(), skeletons);
        proj.animation_session_->selected_keyframe = keyframe_id;
        emit proj.animation_keyframe_selected(keyframe_id);
        emit proj.animation_preview_changed();
        emit proj.refresh_canvas(proj, false);
    };
    cmd.outcome = [state] { return state->status; };
    execute_session_command(cmd);
}

sm::topology_change mdl::project::replace_skeletons_aux(
        const std::vector<sm::object_id>& replacees,
        const std::vector<sm::skel_ref>& replacements,
        const std::unordered_set<sm::object_id>& regenerate_ids,
        const sm::membership_state* membership) {
    auto change = core_.replace_skeletons(replacees, replacements, regenerate_ids, membership);
    if (change.status != sm::result::success)
        return change;
    advance_default_name_counters_from_topology();
    emit refresh_canvas(*this, true);
    return change;
}
sm::result mdl::project::replace_skeletons(
        const std::vector<sm::object_id>& replacees,
        const std::vector<sm::skel_ref>& replacements,
        const std::unordered_set<sm::object_id>& regenerate_ids) {
    auto preview = core_.preview_replace_skeletons(replacees, replacements, regenerate_ids);
    if (!preview)
        return preview.error();
    return execute_command(commands::make_replace_skeletons_command(
        replacees, replacements, regenerate_ids, *preview));
}

bool mdl::identical_pieces(mdl::skel_piece p1, mdl::skel_piece p2) {
    return mdl::to_handle(p1) == mdl::to_handle(p2);
}

sm::result mdl::project::begin_animation_session(sm::object_id character_id, sm::object_id animation_id) {
    if (animation_session_)
        return sm::result::invalid_membership;
    auto character = core_.character(character_id);
    if (!character)
        return character.error();
    auto* source_animation = character->get().animation_data().find_animation(animation_id);
    if (!source_animation)
        return sm::result::not_found;

    animation_edit_session session;
    session.character = character_id;
    session.animation = animation_id;
    session.original_animation_data = character->get().animation_data();
    for (auto skel : character->get().rig().skeletons()) {
        auto copied = skel->copy_to(session.working_topology);
        if (!copied)
            return copied.error();
        copied->get().clear_user_data();
        for (auto node : copied->get().nodes()) node->clear_user_data();
        for (auto bone : copied->get().bones()) bone->clear_user_data();
    }
    if (!source_animation->keyframes.empty())
        session.selected_keyframe = source_animation->keyframes.front().id;
    animation_session_ = std::move(session);
    if (animation_session_->selected_keyframe) {
        const auto* a = core_.animation_data(character_id).find_animation(animation_id);
        sm::apply_skeletal_pose(a->find_keyframe(*animation_session_->selected_keyframe)->pose,
            animation_session_->working_topology, character->get().rig().skeleton_ids());
    }
    emit refresh_undo_redo_state(false, false);
    emit refresh_canvas(*this, true);
    if (animation_session_->selected_keyframe) emit animation_keyframe_selected(*animation_session_->selected_keyframe);
    return sm::result::success;
}
void mdl::project::end_animation_session() {
    if (!animation_session_)
        return;
    exit_animation_preview();
    emit animation_session_ending();
    emit topology_about_to_reset();
    const auto character_id = animation_session_->character;
    const auto before = animation_session_->original_animation_data;
    const auto after = core_.animation_data(character_id);
    const bool changed = animation_session_->authored_depth != 0;
    animation_session_.reset();
    if (changed) {
        command cmd{
            [character_id, after](project& p) { p.core_.animation_data(character_id) = after; },
            [character_id, before](project& p) { p.core_.animation_data(character_id) = before; }
        };
        execute_command(cmd);
    } else {
        emit refresh_undo_redo_state(can_redo(), can_undo());
        emit refresh_canvas(*this, true);
    }
}
std::optional<sm::object_id> mdl::project::animation_session_character() const {
    if (!animation_session_)
        return {};
    return animation_session_->character;
}
std::optional<sm::object_id> mdl::project::animation_session_animation() const {
    if (!animation_session_)
        return {};
    return animation_session_->animation;
}
std::optional<sm::object_id> mdl::project::animation_session_keyframe() const {
    if (!animation_session_)
        return {};
    return animation_session_->selected_keyframe;
}

std::unordered_set<sm::object_id> mdl::project::animation_session_pinned_nodes() const {
    if (!animation_session_)
        return {};
    if (animation_preview_active())
        return playback_pinned_node_ids_;
    if (!animation_session_->selected_keyframe)
        return {};
    const auto* animation = core_.animation_data(animation_session_->character).find_animation(
        animation_session_->animation);
    const auto* keyframe = animation ? animation->find_keyframe(*animation_session_->selected_keyframe) : nullptr;
    return keyframe ? keyframe->pinned_nodes : std::unordered_set<sm::object_id>{};
}

std::unordered_set<sm::object_id> mdl::project::animation_session_incoming_locked_nodes() const {
    if (!animation_session_ || animation_preview_active() || !animation_session_->selected_keyframe)
        return {};
    const auto* animation = core_.animation_data(animation_session_->character).find_animation(animation_session_->animation);
    if (!animation)
        return {};
    auto index = animation->keyframe_index(*animation_session_->selected_keyframe);
    if (!index || *index == 0)
        return {};
    return animation->keyframes[*index - 1].pinned_nodes;
}

std::optional<std::string> mdl::project::animation_session_incoming_lock_source_label(sm::object_id node) const {
    if (!animation_session_ || !animation_session_->selected_keyframe)
        return {};
    const auto* animation = core_.animation_data(animation_session_->character).find_animation(animation_session_->animation);
    if (!animation)
        return {};
    auto index = animation->keyframe_index(*animation_session_->selected_keyframe);
    if (!index || *index == 0 || !animation->keyframes[*index - 1].pinned_nodes.contains(node))
        return {};
    const auto& source = animation->keyframes[*index - 1];
    if (source.name)
        return *source.name;
    return std::string("Pose ") + std::to_string(*index);
}

sm::result mdl::project::set_animation_keyframe_node_pinned(sm::object_id node_id, bool pinned) {
    if (!animation_session_ || !animation_session_->selected_keyframe)
        return sm::result::not_found;
    if (!animation_session_->working_topology.get<sm::node>(node_id))
        return sm::result::invalid_membership;

    const auto character_id = animation_session_->character;
    const auto animation_id = animation_session_->animation;
    const auto keyframe_id = *animation_session_->selected_keyframe;
    auto* animation = core_.animation_data(character_id).find_animation(animation_id);
    auto* keyframe = animation ? animation->find_keyframe(keyframe_id) : nullptr;
    if (!keyframe)
        return sm::result::not_found;
    const bool before = keyframe->pinned_nodes.contains(node_id);
    if (before == pinned)
        return sm::result::success;
    if (pinned) {
        auto index = animation->keyframe_index(keyframe_id);
        if (!index)
            return sm::result::not_found;
        if (*index + 1 < animation->keyframes.size()) {
            const auto skeletons = core_.character(character_id)->get().rig().skeleton_ids();
            auto a = pose_node_position(keyframe->pose, core_.topology(), skeletons, node_id);
            auto b = pose_node_position(animation->keyframes[*index + 1].pose, core_.topology(), skeletons, node_id);
            if (!a || !b || sm::distance(*a, *b) > k_animation_pin_position_tolerance) {
                const auto source_label = keyframe->name ? QString::fromStdString(*keyframe->name) :
                    QStringLiteral("Pose %1").arg(*index + 1);
                const auto destination_label = animation->keyframes[*index + 1].name ?
                    QString::fromStdString(*animation->keyframes[*index + 1].name) :
                    QStringLiteral("Pose %1").arg(*index + 2);
                emit animation_authoring_error(QStringLiteral("Cannot pin this node in %1: its position differs in %2.")
                    .arg(source_label, destination_label));
                return sm::result::invalid_animation;
            }
        }
    }

    auto apply = [character_id, animation_id, keyframe_id, node_id](project& p, bool value) {
        auto* animation = p.core_.animation_data(character_id).find_animation(animation_id);
        auto* keyframe = animation ? animation->find_keyframe(keyframe_id) : nullptr;
        if (!keyframe)
            throw std::runtime_error("animation keyframe missing during pin edit");
        if (value) keyframe->pinned_nodes.insert(node_id);
        else keyframe->pinned_nodes.erase(node_id);
        p.animation_session_->selected_keyframe = keyframe_id;
        emit p.animation_preview_changed();
        emit p.refresh_canvas(p, false);
    };

    command cmd{
        [apply, pinned](project& p) { apply(p, pinned); },
        [apply, before](project& p) { apply(p, before); }
    };
    cmd.animation_edit = true;
    return execute_session_command(cmd);
}

sm::result mdl::project::select_animation_keyframe(sm::object_id keyframe_id) {
    exit_animation_preview();
    if (!animation_session_)
        return sm::result::invalid_membership;
    auto character = core_.character(animation_session_->character);
    if (!character)
        return character.error();
    auto* a = core_.animation_data(animation_session_->character).find_animation(animation_session_->animation);
    auto* k = a ? a->find_keyframe(keyframe_id) : nullptr;
    if (!k)
        return sm::result::not_found;
    sm::apply_skeletal_pose(k->pose, animation_session_->working_topology, character->get().rig().skeleton_ids());
    animation_session_->selected_keyframe = keyframe_id;
    emit animation_keyframe_selected(keyframe_id);
    emit refresh_canvas(*this, false);
    emit animation_preview_changed();
    return sm::result::success;
}

sm::result mdl::project::add_animation_keyframe() {
    exit_animation_preview();
    if (!animation_session_)
        return sm::result::invalid_membership;

    const auto character_id = animation_session_->character;
    const auto animation_id = animation_session_->animation;
    auto* animation = core_.animation_data(character_id).find_animation(animation_id);
    if (!animation)
        return sm::result::not_found;

    const auto skeletons = core_.character(character_id)->get().rig().skeleton_ids();
    const auto before = *animation;
    auto after = before;

    sm::pose_keyframe keyframe;
    keyframe.pose = after.keyframes.empty() ?
        sm::capture_skeletal_pose(topology(), skeletons) : after.keyframes.back().pose;
    if (!after.keyframes.empty())
        keyframe.pinned_nodes = after.keyframes.back().pinned_nodes;
    const auto keyframe_id = keyframe.id;
    after.keyframes.push_back(std::move(keyframe));
    after.reconcile_transitions();
    if (validate_pin_endpoints(after, core_.topology(), skeletons) != sm::result::success) {
        emit animation_authoring_error(QStringLiteral("Cannot add this pose because it would create mismatched pinned endpoints."));
        return sm::result::invalid_animation;
    }

    auto apply = [character_id, animation_id, keyframe_id, skeletons](
            project& p, const sm::animation& value) {
        auto* target = p.core_.animation_data(character_id).find_animation(animation_id);
        if (!target)
            throw std::runtime_error("animation missing");

        *target = value;
        p.animation_session_->selected_keyframe = keyframe_id;
        sm::apply_skeletal_pose(target->find_keyframe(keyframe_id)->pose,
            p.topology(), skeletons);
        emit p.animation_keyframe_selected(keyframe_id);
        emit p.animation_preview_changed();
        emit p.refresh_canvas(p, false);
    };

    command cmd;
    cmd.redo = [after, apply](project& p) {
        apply(p, after);
    };
    cmd.animation_edit = true;
    cmd.undo = [before, character_id, animation_id, skeletons](project& p) {
        auto* target = p.core_.animation_data(character_id).find_animation(animation_id);
        if (!target)
            throw std::runtime_error("animation missing");
        *target = before;

        if (!before.keyframes.empty()) {
            const auto previous_id = before.keyframes.back().id;
            p.animation_session_->selected_keyframe = previous_id;
            sm::apply_skeletal_pose(before.keyframes.back().pose, p.topology(), skeletons);
            emit p.animation_keyframe_selected(previous_id);
        } else {
            p.animation_session_->selected_keyframe.reset();
        }
        emit p.animation_preview_changed();
        emit p.refresh_canvas(p, false);
    };
    return execute_session_command(cmd);
}

sm::result mdl::project::duplicate_animation_keyframe() {
    exit_animation_preview();
    if (!animation_session_ || !animation_session_->selected_keyframe) {
        return sm::result::not_found;
    }

    const auto character_id = animation_session_->character;
    const auto animation_id = animation_session_->animation;
    const auto selected_id = *animation_session_->selected_keyframe;
    auto* animation = core_.animation_data(character_id).find_animation(animation_id);
    if (!animation)
        return sm::result::not_found;

    auto index = animation->keyframe_index(selected_id);
    if (!index)
        return sm::result::not_found;

    const auto before = *animation;
    auto after = before;
    auto copy = after.keyframes[*index];
    copy.id = sm::object_id::generate();
    const auto copy_id = copy.id;
    after.keyframes.insert(after.keyframes.begin() + *index + 1, std::move(copy));

    if (*index < after.transitions.size()) {
        after.transitions.insert(after.transitions.begin() + *index, sm::pose_transition{});
    } else {
        after.transitions.emplace_back();
    }

    const auto skeletons = core_.character(character_id)->get().rig().skeleton_ids();
    if (validate_pin_endpoints(after, core_.topology(), skeletons) != sm::result::success) {
        emit animation_authoring_error(QStringLiteral("Cannot duplicate this pose because it would create mismatched pinned endpoints."));
        return sm::result::invalid_animation;
    }
    command cmd{
        [character_id, animation_id, copy_id, after, skeletons](project& p) {
            auto* target = p.core_.animation_data(character_id).find_animation(animation_id);
            if (!target)
                throw std::runtime_error("animation missing");
            *target = after;
            p.animation_session_->selected_keyframe = copy_id;
            sm::apply_skeletal_pose(target->find_keyframe(copy_id)->pose,
                p.topology(), skeletons);
            emit p.animation_keyframe_selected(copy_id);
            emit p.animation_preview_changed();
            emit p.refresh_canvas(p, false);
        },
        [character_id, animation_id, selected_id, before, skeletons](project& p) {
            auto* target = p.core_.animation_data(character_id).find_animation(animation_id);
            if (!target)
                throw std::runtime_error("animation missing");
            *target = before;
            p.animation_session_->selected_keyframe = selected_id;
            sm::apply_skeletal_pose(target->find_keyframe(selected_id)->pose,
                p.topology(), skeletons);
            emit p.animation_keyframe_selected(selected_id);
            emit p.animation_preview_changed();
            emit p.refresh_canvas(p, false);
        }
    };
    cmd.animation_edit = true;
    return execute_session_command(cmd);
}

sm::result mdl::project::set_animation_transition_duration(sm::object_id transition_id, double seconds) {
    if (!animation_session_)
        return sm::result::invalid_membership;
    if (!(seconds > 0.0) || !std::isfinite(seconds))
        return sm::result::invalid_animation;
    const auto character_id = animation_session_->character;
    const auto animation_id = animation_session_->animation;
    auto* animation = core_.animation_data(character_id).find_animation(animation_id);
    if (!animation)
        return sm::result::not_found;
    auto it = std::find_if(animation->transitions.begin(), animation->transitions.end(),
        [transition_id](const auto& t) { return t.id == transition_id; });
    if (it == animation->transitions.end())
        return sm::result::not_found;
    const double before = it->duration_seconds;
    if (before == seconds)
        return sm::result::success;
    double total = animation->duration_seconds() - before + seconds;
    if (!std::isfinite(total))
        return sm::result::invalid_animation;
    auto apply = [character_id, animation_id, transition_id](project& p, double value) {
        auto* a = p.core_.animation_data(character_id).find_animation(animation_id);
        if (!a)
            throw std::runtime_error("animation missing");
        auto t = std::find_if(a->transitions.begin(), a->transitions.end(),
            [transition_id](const auto& candidate) { return candidate.id == transition_id; });
        if (t == a->transitions.end())
            throw std::runtime_error("transition missing");
        t->duration_seconds = value;
        emit p.animation_preview_changed();
    };
    command cmd{[apply, seconds](project& p){ apply(p, seconds); },
                [apply, before](project& p){ apply(p, before); }};
    cmd.animation_edit = true;
    return execute_session_command(cmd);
}

sm::result mdl::project::insert_animation_keyframe(double seconds) {
    if (!animation_session_ || !std::isfinite(seconds))
        return sm::result::invalid_animation;
    const auto character_id = animation_session_->character;
    const auto animation_id = animation_session_->animation;
    auto* animation = core_.animation_data(character_id).find_animation(animation_id);
    if (!animation || animation->keyframes.size() < 2)
        return sm::result::invalid_animation;
    const auto skeletons = core_.character(character_id)->get().rig().skeleton_ids();
    if (validate_pin_endpoints(*animation, core_.topology(), skeletons) != sm::result::success) {
        emit animation_authoring_error(QStringLiteral("Cannot insert into a transition with mismatched pinned endpoints. Remove the offending source pin first."));
        return sm::result::invalid_animation;
    }

    double start = 0.0;
    std::optional<std::size_t> interval;
    for (std::size_t i = 0; i < animation->transitions.size(); ++i) {
        const double end = start + animation->transitions[i].duration_seconds;
        if (seconds > start && seconds < end) {
            interval = i;
            break;
        }
        start = end;
    }
    if (!interval)
        return sm::result::invalid_animation;
    const double original_duration = animation->transitions[*interval].duration_seconds;
    const double first_duration = seconds - start;
    const double second_duration = original_duration - first_duration;
    const double duration_tolerance = std::max(1e-12, std::abs(original_duration) * 1e-12);
    if (!(first_duration > 0.0) || !(second_duration > 0.0) ||
            !std::isfinite(first_duration) || !std::isfinite(second_duration) ||
            std::abs((first_duration + second_duration) - original_duration) > duration_tolerance)
        return sm::result::invalid_animation;

    auto sampled = sm::sample_constrained_pose(*animation, seconds, core_.topology(), skeletons);
    if (!sampled || !*sampled || !std::holds_alternative<sm::reference_transition>((**sampled).location))
        return sampled ? sm::result::invalid_animation : sampled.error();

    const auto before = *animation;
    auto after = before;
    sm::pose_keyframe inserted;
    inserted.pose = (**sampled).pose;
    inserted.pinned_nodes = after.keyframes[*interval].pinned_nodes;
    const auto inserted_id = inserted.id;
    after.keyframes.insert(after.keyframes.begin() + *interval + 1, inserted);
    after.transitions[*interval].duration_seconds = first_duration; // preserve original ID
    sm::pose_transition second;
    second.duration_seconds = second_duration;
    after.transitions.insert(after.transitions.begin() + *interval + 1, second);
    if (validate_pin_endpoints(after, core_.topology(), skeletons) != sm::result::success) {
        emit animation_authoring_error(QStringLiteral("The sampled pose cannot be inserted without violating pinned endpoints."));
        return sm::result::invalid_animation;
    }

    const auto previous_selection = animation_session_->selected_keyframe;
    auto apply = [character_id, animation_id, skeletons](project& p, const sm::animation& value,
            std::optional<sm::object_id> selection) {
        auto* a = p.core_.animation_data(character_id).find_animation(animation_id);
        if (!a)
            throw std::runtime_error("animation missing");
        *a = value;
        p.animation_session_->selected_keyframe = selection;
        if (selection) {
            auto* k = a->find_keyframe(*selection);
            if (!k)
                throw std::runtime_error("keyframe missing");
            sm::apply_skeletal_pose(k->pose, p.topology(), skeletons);
            emit p.animation_keyframe_selected(*selection);
        }
        emit p.animation_preview_changed();
        emit p.refresh_canvas(p, false);
    };
    command cmd{[apply, after, inserted_id](project& p){ apply(p, after, inserted_id); },
                [apply, before, previous_selection](project& p){ apply(p, before, previous_selection); }};
    cmd.animation_edit = true;
    return execute_session_command(cmd);
}

sm::result mdl::project::rename_animation_keyframe(const std::optional<std::string>& name) {
    exit_animation_preview();
    if (!animation_session_ || !animation_session_->selected_keyframe) {
        return sm::result::not_found;
    }

    const auto character_id = animation_session_->character;
    const auto animation_id = animation_session_->animation;
    const auto keyframe_id = *animation_session_->selected_keyframe;
    auto* animation = core_.animation_data(character_id).find_animation(animation_id);
    auto* keyframe = animation ? animation->find_keyframe(keyframe_id) : nullptr;
    if (!keyframe)
        return sm::result::not_found;

    const auto old_name = keyframe->name;
    command cmd{
        [character_id, animation_id, keyframe_id, name](project& p) {
            auto* target = p.core_.animation_data(character_id).find_animation(animation_id);
            if (!target)
                throw std::runtime_error("animation missing");
            auto* keyframe = target->find_keyframe(keyframe_id);
            if (!keyframe)
                throw std::runtime_error("animation keyframe missing");
            keyframe->name = name;
            p.animation_session_->selected_keyframe = keyframe_id;
            emit p.animation_keyframe_selected(keyframe_id);
            emit p.animation_preview_changed();
        },
        [character_id, animation_id, keyframe_id, old_name](project& p) {
            auto* target = p.core_.animation_data(character_id).find_animation(animation_id);
            if (!target)
                throw std::runtime_error("animation missing");
            auto* keyframe = target->find_keyframe(keyframe_id);
            if (!keyframe)
                throw std::runtime_error("animation keyframe missing");
            keyframe->name = old_name;
            p.animation_session_->selected_keyframe = keyframe_id;
            emit p.animation_keyframe_selected(keyframe_id);
            emit p.animation_preview_changed();
        }
    };
    cmd.animation_edit = true;
    return execute_session_command(cmd);
}

sm::result mdl::project::delete_animation_keyframe() {
    exit_animation_preview();
    if (!animation_session_ || !animation_session_->selected_keyframe) {
        return sm::result::not_found;
    }

    const auto character_id = animation_session_->character;
    const auto animation_id = animation_session_->animation;
    const auto keyframe_id = *animation_session_->selected_keyframe;
    auto* animation = core_.animation_data(character_id).find_animation(animation_id);
    if (!animation)
        return sm::result::not_found;

    auto index = animation->keyframe_index(keyframe_id);
    if (!index)
        return sm::result::not_found;

    const auto before = *animation;
    auto after = before;
    after.keyframes.erase(after.keyframes.begin() + *index);
    if (!after.transitions.empty()) {
        const auto transition_index = *index == 0 ? std::size_t{0} : *index - 1;
        after.transitions.erase(after.transitions.begin() +
            std::min(transition_index, after.transitions.size() - 1));
    }

    std::optional<sm::object_id> next;
    if (!after.keyframes.empty()) {
        next = after.keyframes[std::min(*index, after.keyframes.size() - 1)].id;
    }

    const auto skeletons = core_.character(character_id)->get().rig().skeleton_ids();
    if (validate_pin_endpoints(after, core_.topology(), skeletons) != sm::result::success) {
        emit animation_authoring_error(QStringLiteral("Cannot delete this pose because it would create mismatched pinned endpoints."));
        return sm::result::invalid_animation;
    }
    command cmd{
        [character_id, animation_id, after, next, skeletons](project& p) {
            auto* target = p.core_.animation_data(character_id).find_animation(animation_id);
            if (!target)
                throw std::runtime_error("animation missing");
            *target = after;
            p.animation_session_->selected_keyframe = next;
            if (next) {
                sm::apply_skeletal_pose(target->find_keyframe(*next)->pose,
                    p.topology(), skeletons);
                emit p.animation_keyframe_selected(*next);
            }
            emit p.animation_preview_changed();
            emit p.refresh_canvas(p, false);
        },
        [character_id, animation_id, before, keyframe_id, skeletons](project& p) {
            auto* target = p.core_.animation_data(character_id).find_animation(animation_id);
            if (!target)
                throw std::runtime_error("animation missing");
            *target = before;
            p.animation_session_->selected_keyframe = keyframe_id;
            sm::apply_skeletal_pose(target->find_keyframe(keyframe_id)->pose,
                p.topology(), skeletons);
            emit p.animation_keyframe_selected(keyframe_id);
            emit p.animation_preview_changed();
            emit p.refresh_canvas(p, false);
        }
    };
    cmd.animation_edit = true;
    return execute_session_command(cmd);
}

void mdl::project::set_show_previous_pose(bool show) {
    if (show_previous_pose_ == show)
        return;
    show_previous_pose_ = show;
    emit refresh_canvas(*this, false);
}

void mdl::project::edit_animation_data(sm::object_id id, const std::function<void(sm::animation_assets&)>& edit) {
    auto before = core_.animation_data(id), after = before;
    edit(after);
    const auto character = core_.character(id);
    if (!character)
        throw std::invalid_argument("Missing character");
    after.validate(core_.topology(), character->get().rig().skeleton_ids());
    execute_command({[id, after](project& p) { p.core_.animation_data(id) = after; },
        [id, before](project& p) { p.core_.animation_data(id) = before; }});
}
void mdl::project::apply_pose(sm::object_id character, sm::object_id id) {
    if (animation_mode())
        return;
    const auto& c = core_.character(character).value().get();
    const auto* pose = c.animation_data().find_pose(id);
    if (!pose || !sm::pose_compatible(*pose, topology(), c.rig().skeleton_ids()))
        throw std::invalid_argument("Pose does not match the current rig. Update or recreate it first.");
    node_locs before, after;
    for (const auto& [nid, pt] : pose->node_positions) {
        before.emplace_back(nid, topology().get<sm::node>(nid)->get().world_pos());
        after.emplace_back(nid, pt);
    }
    transform_node_positions(before, after);
}
