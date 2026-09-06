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
    using object_id_set = std::unordered_set<sm::object_id>;

    object_id_set live_object_ids(const sm::world& world) {
        object_id_set ids;
        for (auto skel : world.skeletons()) {
            ids.insert(skel->id());
            for (auto node : skel->nodes()) {
                ids.insert(node->id());
            }
            for (auto bone : skel->bones()) {
                ids.insert(bone->id());
            }
        }
        return ids;
    }
    sm::object_id unused_object_id(object_id_set& used) {
        while (true) {
            auto id = sm::object_id::generate();
            if (used.insert(id).second) {
                return id;
            }
        }
    }

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
const sm::world& mdl::project::world() const { return core_.world(); }
sm::world& mdl::project::world() { return core_.world(); }

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
void mdl::project::advance_default_name_counters_from_world() {
    const auto& topology = std::as_const(core_).world();
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
    advance_default_name_counters_from_world();
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
void mdl::project::rename_aux(skel_piece piece_var, const std::string& new_name) {
    std::visit(
        [&](auto ref) {
            auto& piece = ref.get();
            piece.owner().set_name(piece, new_name);
        },
        piece_var
    );
    advance_default_name_counters_from_world();
    emit name_changed(piece_var, new_name);
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
void mdl::project::replace_skeletons_aux(
        const std::vector<sm::object_id>& replacees,
        const std::vector<sm::skel_ref>& replacements,
        std::vector<sm::object_id>* new_ids,
        const std::unordered_set<sm::object_id>& regenerate_ids) {
    for (const auto& replacee : replacees) {
        world().delete_skeleton(replacee);
    }

    auto used_ids = live_object_ids(world());
    auto allocation_guard = used_ids;
    for (auto replacement : replacements) {
        allocation_guard.insert(replacement->id());
        for (auto node : replacement->nodes()) {
            allocation_guard.insert(node->id());
        }
        for (auto bone : replacement->bones()) {
            allocation_guard.insert(bone->id());
        }
    }

    for (auto replacement : replacements) {
        std::unordered_map<sm::object_id, sm::object_id> id_remap;
        auto reserve_id = [&](const sm::object_id& id) {
            if (regenerate_ids.contains(id) || used_ids.contains(id)) {
                auto new_id = unused_object_id(allocation_guard);
                used_ids.insert(new_id);
                id_remap[id] = new_id;
            } else {
                used_ids.insert(id);
            }
        };
        reserve_id(replacement->id());
        for (auto node : replacement->nodes()) {
            reserve_id(node->id());
        }
        for (auto bone : replacement->bones()) {
            reserve_id(bone->id());
        }

        auto new_skel = replacement->copy_to(world(), id_remap);
        if (!new_skel) {
            throw std::runtime_error("skeleton copy failed");
        }
        if (new_ids) {
            new_ids->push_back(new_skel->get().id());
        }
    }
    if (!core_.has_unique_object_ids()) {
        throw std::runtime_error("live project contains duplicate object IDs");
    }
    advance_default_name_counters_from_world();
    emit refresh_canvas(*this, true);
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
