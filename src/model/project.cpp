#include "project.hpp"
#include "commands.hpp"
#include "../core/sm_project.hpp"
#include <charconv>
#include <algorithm>
#include <system_error>
#include <type_traits>
#include <string_view>
#include <stdexcept>
#include <unordered_set>
#include <unordered_map>
#include <utility>

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
}
/*------------------------------------------------------------------------------------------------*/
void mdl::project::clear_redo_stack() { redo_stack_ = {}; }
void mdl::project::emit_history_state(bool was_dirty) {
    emit refresh_undo_redo_state(can_redo(), can_undo());
    const bool dirty = is_dirty();
    if (dirty != was_dirty) emit dirty_changed(dirty);
}
void mdl::project::notify_command_change(const command& cmd) {
    if (cmd.artwork_character)
        emit artwork_changed(*this, *cmd.artwork_character);
    else
        emit project_changed(*this);
}
sm::result mdl::project::execute_command(const command& cmd) {
    if (animation_mode_ && !cmd.animation_edit) return sm::result::invalid_membership;
    const bool was_dirty = is_dirty();
    command stored = cmd;
    stored.redo(*this);
    if (stored.outcome && stored.outcome() != sm::result::success) return stored.outcome();
    clear_redo_stack();
    animation_redo_count_ = 0;
    stored.history_transition = history_.advance();
    undo_stack_.push(stored);
    emit_history_state(was_dirty);
    notify_command_change(stored);
    return sm::result::success;
}

mdl::project::project() {}

void mdl::project::set_topology_edit_confirmation(
        std::function<bool(const sm::topology_edit_effects&)> confirmation) {
    topology_edit_confirmation_ = std::move(confirmation);
}

bool mdl::project::confirm_topology_edit(const sm::topology_edit_effects& effects) const {
    return !effects.has_animation_cascade() || !topology_edit_confirmation_ || topology_edit_confirmation_(effects);
}

const sm::project& mdl::project::core() const { return core_; }
sm::project& mdl::project::core() { return core_; }
void mdl::project::edit_artwork(const sm::object_id& id, const std::function<void(sm::artwork&)>& edit) {
    if (animation_mode_) return;
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
const sm::topology& mdl::project::topology() const { return core_.topology(); }

mdl::model_object mdl::project::get(const sm::object_id& id) {
    return core_.get(id);
}

mdl::const_model_object mdl::project::get(const sm::object_id& id) const {
    return core_.get(id);
}

void mdl::project::clear() {
    core_.clear();
    redo_stack_ = {};
    undo_stack_ = {};
    history_.reset_clean();
    animation_mode_ = false;
    animation_undo_depth_ = 0;
    animation_redo_count_ = 0;
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
    if (!can_undo()) {
        return;
    }
    const bool was_dirty = is_dirty();
    auto cmd = undo_stack_.top();
    undo_stack_.pop();
    cmd.undo(*this);
    history_.undo(cmd.history_transition);
    redo_stack_.push(cmd);
    if (animation_mode_) ++animation_redo_count_;
    emit_history_state(was_dirty);
    notify_command_change(cmd);
}
sm::result mdl::project::redo() {
    if (!can_redo()) {
        return sm::result::success;
    }
    const bool was_dirty = is_dirty();
    auto cmd = redo_stack_.top();
    if (animation_mode_ && !cmd.animation_edit) return sm::result::invalid_membership;
    cmd.redo(*this);
    if (cmd.outcome && cmd.outcome() != sm::result::success) return cmd.outcome();
    redo_stack_.pop();
    if (animation_mode_) --animation_redo_count_;
    history_.redo(cmd.history_transition);
    undo_stack_.push(cmd);
    emit_history_state(was_dirty);
    notify_command_change(cmd);
    return sm::result::success;
}
bool mdl::project::can_undo() const {
    return !undo_stack_.empty() && (!animation_mode_ ||
        (undo_stack_.size() > animation_undo_depth_ && undo_stack_.top().animation_edit));
}
bool mdl::project::can_redo() const {
    return !redo_stack_.empty() && (!animation_mode_ || (animation_redo_count_ > 0 && redo_stack_.top().animation_edit));
}
bool mdl::project::is_dirty() const noexcept {
    return history_.dirty();
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
    animation_undo_depth_ = 0;
    animation_redo_count_ = 0;
    next_node_name_ = 1;
    next_bone_name_ = 1;
    advance_default_name_counters_from_topology();
    emit_history_state(was_dirty);
    emit new_project_opened(*this);
    return sm::project_result::success;
}
bool mdl::project::deserialize(std::span<const std::uint8_t> buffer) {
    if (animation_mode_) return false;
    return deserialize_result(buffer) == sm::project_result::success;
}
sm::result mdl::project::add_bone(const handle& u, const handle& v) {
    auto& node_u = commands::resolve<sm::node>(*this, u);
    auto& node_v = commands::resolve<sm::node>(*this, v);
    auto preview = core_.preview_create_bone(node_u, node_v);
    if (!preview) return preview.error();
    if (!confirm_topology_edit(*preview)) return sm::result::cancelled;
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
                for (auto s : candidates) ids.push_back(s->id());
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
    if (animation_mode_) return;
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
                for (auto s : candidates) ids.push_back(s->id());
                state->before = proj.core_.snapshot_membership(ids);
                auto created = proj.core_.create_character(candidates);
                if (!created) { state->status = created.error(); return; }
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
    if (result != sm::result::success) return std::unexpected(result);
    return state->id;
}

std::expected<sm::object_id, sm::result> mdl::project::paste_character(
        const sm::topology& rig, const std::string& name, const sm::artwork& artwork,
        sm::object_id character_root_bone, const sm::animation_assets& animation_data) {
    if (rig.empty()) return std::unexpected(sm::result::empty_character);
    struct state_type {
        sm::topology topology;
        sm::membership_state membership;
        std::vector<sm::object_id> ids;
        sm::object_id character = sm::object_id::generate();
        sm::result status = sm::result::success;
    };
    auto state = std::make_shared<state_type>();
    std::unordered_set<std::string> existing_names;
    for (auto character : core_.characters()) existing_names.insert(character->name());
    auto copied_name = name + " copy";
    for (std::size_t suffix = 2; existing_names.contains(copied_name); ++suffix)
        copied_name = name + " copy " + std::to_string(suffix);
    std::unordered_map<sm::object_id, sm::object_id> remap;
    for (auto skel : rig.skeletons()) {
        remap.emplace(skel->id(), sm::object_id::generate());
        for (auto node : skel->nodes()) remap.emplace(node->id(), sm::object_id::generate());
        for (auto bone : skel->bones()) remap.emplace(bone->id(), sm::object_id::generate());
    }
    auto copied_artwork = artwork;
    copied_artwork.remap_bones(remap);
    auto copied_animation_data = animation_data;
    sm::remap_animation_assets(copied_animation_data, remap);
    if(auto it=remap.find(character_root_bone);it!=remap.end()) character_root_bone=it->second;
    else character_root_bone={};
    state->membership.characters.push_back({state->character, copied_name, character_root_bone,
        std::move(copied_artwork), std::move(copied_animation_data)});
    for (auto skel : rig.skeletons()) {
        auto copy = skel->copy_to(state->topology, remap);
        if (!copy) return std::unexpected(copy.error());
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
    if (result != sm::result::success) return std::unexpected(result);
    return state->character;
}

sm::result mdl::project::delete_character(const sm::object_id& id) {
    auto character = core_.character(id);
    if (!character) return character.error();
    // Stage 2 replacement is the semantic structural deletion operation: it prunes
    // the empty character and snapshots both identity and membership for undo.
    return replace_skeletons(character->get().rig().skeleton_ids(), {});
}

sm::result mdl::project::set_character_root_bone(const sm::object_id& character_id,const sm::object_id& bone_id) {
    auto character=core_.character(character_id);
    if(!character) return character.error();
    const auto before=character->get().character_root_bone();
    if(before==bone_id) return sm::result::success;
    struct state_type { sm::result status=sm::result::success; };
    auto state=std::make_shared<state_type>();
    return execute_command({
        [state,character_id,bone_id](project& proj) {
            state->status=proj.core_.set_character_root_bone(character_id,bone_id);
        },
        [before,character_id](project& proj) {
            if(proj.core_.set_character_root_bone(character_id,before)!=sm::result::success)
                throw std::runtime_error("unable to restore character root bone");
        },
        [state] { return state->status; }
    });
}

bool mdl::project::rename(const sm::object_id& id, const std::string& new_name) {
    auto old_name = std::visit([](auto ref) { return ref->name(); }, std::as_const(core_).get(id));
    if (old_name == new_name) return true;
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
        if constexpr (!std::is_same_v<value_type, sm::character>) {
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
    execute_command(commands::make_transform_bones_or_nodes_command(*this, nodes, {}, fn, {}));
}
void mdl::project::transform(const std::vector<handle>& bones,
        const std::function<void(sm::bone&)>& fn) {
    execute_command(commands::make_transform_bones_or_nodes_command(*this, {}, bones, {}, fn));
}
void mdl::project::transform_node_positions(
        const node_locs& old_locs, const node_locs& new_locs) {
    execute_command(commands::make_transform_node_positions_command(*this, old_locs, new_locs));
}
sm::topology_change mdl::project::replace_skeletons_aux(
        const std::vector<sm::object_id>& replacees,
        const std::vector<sm::skel_ref>& replacements,
        const std::unordered_set<sm::object_id>& regenerate_ids,
        const sm::membership_state* membership) {
    auto change = core_.replace_skeletons(replacees, replacements, regenerate_ids, membership);
    if (change.status != sm::result::success) return change;
    advance_default_name_counters_from_topology();
    emit refresh_canvas(*this, true);
    return change;
}
sm::result mdl::project::replace_skeletons(
        const std::vector<sm::object_id>& replacees,
        const std::vector<sm::skel_ref>& replacements,
        const std::unordered_set<sm::object_id>& regenerate_ids) {
    auto preview = core_.preview_replace_skeletons(replacees, replacements, regenerate_ids);
    if (!preview) return preview.error();
    if (!confirm_topology_edit(*preview)) return sm::result::cancelled;
    return execute_command(commands::make_replace_skeletons_command(
        replacees, replacements, regenerate_ids, *preview));
}

bool mdl::identical_pieces(mdl::skel_piece p1, mdl::skel_piece p2) {
    return mdl::to_handle(p1) == mdl::to_handle(p2);
}

void mdl::project::set_animation_mode(bool active) {
    if (active && !animation_mode_) animation_undo_depth_ = undo_stack_.size();
    if (active && !animation_mode_) animation_redo_count_ = 0;
    animation_mode_ = active;
    emit refresh_undo_redo_state(can_redo(), can_undo());
}
void mdl::project::edit_animation_data(sm::object_id id, const std::function<void(sm::animation_assets&)>& edit) {
    auto before = core_.animation_data(id), after = before;
    edit(after);
    const auto character = core_.character(id);
    if (!character) throw std::invalid_argument("Missing character");
    after.validate(core_.topology(), character->get().rig().skeleton_ids(),
        character->get().character_root_bone());
    execute_command({[id, after](project& p) { p.core_.animation_data(id) = after; },
        [id, before](project& p) { p.core_.animation_data(id) = before; }, {}, true});
}
void mdl::project::apply_pose(sm::object_id character, sm::object_id id) {
    if (animation_mode_) return;
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
