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
sm::result mdl::project::execute_command(const command& cmd) {
    cmd.redo(*this);
    if (cmd.outcome && cmd.outcome() != sm::result::success) return cmd.outcome();
    clear_redo_stack();
    undo_stack_.push(cmd);
    emit refresh_undo_redo_state(can_redo(), can_undo());
    emit project_changed(*this);
    return sm::result::success;
}

mdl::project::project() {}

const sm::project& mdl::project::core() const { return core_; }
sm::project& mdl::project::core() { return core_; }
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
    auto cmd = undo_stack_.top();
    undo_stack_.pop();
    cmd.undo(*this);
    redo_stack_.push(cmd);
    emit refresh_undo_redo_state(can_redo(), can_undo());
    emit project_changed(*this);
}
sm::result mdl::project::redo() {
    if (!can_redo()) {
        return sm::result::success;
    }
    auto cmd = redo_stack_.top();
    cmd.redo(*this);
    if (cmd.outcome && cmd.outcome() != sm::result::success) return cmd.outcome();
    redo_stack_.pop();
    undo_stack_.push(cmd);
    emit refresh_undo_redo_state(can_redo(), can_undo());
    emit project_changed(*this);
    return sm::result::success;
}
bool mdl::project::can_undo() const { return !undo_stack_.empty(); }
bool mdl::project::can_redo() const { return !redo_stack_.empty(); }
std::expected<sm::project_buffer, sm::project_result> mdl::project::serialize() const {
    return core_.serialize();
}
bool mdl::project::deserialize(std::span<const std::uint8_t> buffer) {
    auto result = core_.deserialize(buffer);
    if (result != sm::project_result::success) {
        return false;
    }
    redo_stack_ = {};
    undo_stack_ = {};
    next_node_name_ = 1;
    next_bone_name_ = 1;
    advance_default_name_counters_from_topology();
    emit refresh_undo_redo_state(false, false);
    emit new_project_opened(*this);
    return true;
}
sm::result mdl::project::add_bone(const handle& u, const handle& v) {
    auto status = core_.can_create_bone(commands::resolve<sm::node>(*this, u), commands::resolve<sm::node>(*this, v));
    if (status != sm::result::success) return status;
    return execute_command(commands::make_add_bone_command(u, v, next_default_bone_name()));
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
                state->before = proj.core_.snapshot_membership(ids);
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
        const sm::topology& rig, const std::string& name) {
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
    state->membership.characters.push_back({state->character, copied_name});
    for (auto skel : rig.skeletons()) {
        auto copy = skel->duplicate_to(state->topology);
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
    auto plan = core_.plan_replacement(replacees, replacements);
    if (!plan) return plan.error();
    return execute_command(commands::make_replace_skeletons_command(
        replacees, replacements, regenerate_ids));
}

bool mdl::identical_pieces(mdl::skel_piece p1, mdl::skel_piece p2) {
    return mdl::to_handle(p1) == mdl::to_handle(p2);
}
