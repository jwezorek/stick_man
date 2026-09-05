#include "project.hpp"
#include "commands.hpp"
#include "../core/sm_skeleton.hpp"
#include "../core/json.hpp"
#include <optional>
#include <charconv>
#include <algorithm>
#include <system_error>
#include <type_traits>
#include <string_view>
#include <stdexcept>

using json = nlohmann::json;

/*------------------------------------------------------------------------------------------------*/
namespace {
    std::optional<sm::world> json_to_world(const std::string& str) {
        try {
            json proj = json::parse(str);
            if (proj.at("version").get<double>() != 2.0) {
                return {};
            }
            sm::world new_world;
            auto result = new_world.from_json(proj.at("world"));
            if (result != sm::result::success) {
                return {};
            }
            return std::move(new_world);
        } catch (...) {
            return {};
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

const sm::world& mdl::project::world() const { return world_; }
sm::world& mdl::project::world() { return world_; }

void mdl::project::clear() {
    world_.clear();
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
    for (auto skel : world_.skeletons()) {
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
std::string mdl::project::to_json() const {
    json stick_man_project = {
        {"version", 2.0},
        {"world", world_.to_json()}
    };
    return stick_man_project.dump(4);
}
bool mdl::project::from_json(const std::string& str) {
    auto new_world = json_to_world(str);
    if (!new_world) {
        return false;
    }
    clear();
    world_ = std::move(*new_world);
    advance_default_name_counters_from_world();
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
        std::vector<sm::object_id>* new_ids) {
    for (const auto& replacee : replacees) {
        world_.delete_skeleton(replacee);
    }
    for (auto replacement : replacements) {
        auto new_skel = replacement->copy_to(world_);
        if (!new_skel) {
            throw std::runtime_error("skeleton copy failed");
        }
        if (new_ids) {
            new_ids->push_back(new_skel->get().id());
        }
    }
    advance_default_name_counters_from_world();
    emit refresh_canvas(*this, true);
}
void mdl::project::replace_skeletons(
        const std::vector<sm::object_id>& replacees,
        const std::vector<sm::skel_ref>& replacements) {
    execute_command(commands::make_replace_skeletons_command(replacees, replacements));
}

bool mdl::identical_pieces(mdl::skel_piece p1, mdl::skel_piece p2) {
    return mdl::to_handle(p1) == mdl::to_handle(p2);
}
