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
void mdl::project::execute_command(const command& cmd) {
    clear_redo_stack();
    cmd.redo(*this);
    undo_stack_.push(cmd);
    emit refresh_undo_redo_state(can_redo(), can_undo());
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
}
void mdl::project::redo() {
    if (!can_redo()) {
        return;
    }
    auto cmd = redo_stack_.top();
    redo_stack_.pop();
    cmd.redo(*this);
    undo_stack_.push(cmd);
    emit refresh_undo_redo_state(can_redo(), can_undo());
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
void mdl::project::add_bone(const handle& u, const handle& v) {
    execute_command(commands::make_add_bone_command(u, v, next_default_bone_name()));
}
void mdl::project::add_new_skeleton_root(sm::point loc) {
    execute_command(commands::make_create_node_command(loc, next_default_node_name()));
}
void mdl::project::rename_aux(handle id, const std::string& new_name) {
    core_.rename(id, new_name);
    advance_default_name_counters_from_topology();
    emit name_changed(std::as_const(core_).get(id), new_name);
}
bool mdl::project::can_rename(skel_piece, const std::string&) {
    return true;
}
bool mdl::project::rename(skel_piece piece, const std::string& new_name) {
    std::visit(
        [this, piece, new_name](auto ref) {
            using value_type = std::remove_cvref_t<decltype(ref.get())>;
            execute_command(commands::make_rename_command<value_type>(ref, new_name));
        },
        piece
    );
    return true;
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
        const std::unordered_set<sm::object_id>& regenerate_ids) {
    auto change = core_.replace_skeletons(replacees, replacements, regenerate_ids);
    advance_default_name_counters_from_topology();
    emit refresh_canvas(*this, true);
    return change;
}
void mdl::project::replace_skeletons(
        const std::vector<sm::object_id>& replacees,
        const std::vector<sm::skel_ref>& replacements,
        const std::unordered_set<sm::object_id>& regenerate_ids) {
    execute_command(commands::make_replace_skeletons_command(
        replacees, replacements, regenerate_ids));
}

bool mdl::identical_pieces(mdl::skel_piece p1, mdl::skel_piece p2) {
    return mdl::to_handle(p1) == mdl::to_handle(p2);
}
