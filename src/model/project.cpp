#include "project.hpp"
#include "commands.hpp"
#include "../core/sm_skeleton.hpp"
#include "../core/json.hpp"
#include <ranges>
#include <optional>
#include <tuple>
#include <charconv>
#include <algorithm>
#include <system_error>

using json = nlohmann::json;
namespace r = std::ranges;
namespace rv = std::ranges::views;

/*------------------------------------------------------------------------------------------------*/

namespace {
    json tabs_to_json(const std::unordered_map<std::string, std::vector<sm::object_id>>& tabs) {
        json result = json::array();
        for (const auto& [tab, skeletons] : tabs) {
            json ids = json::array();
            for (const auto& id : skeletons) {
                ids.push_back(id.to_string());
            }
            result.push_back({{"tab", tab}, {"skeletons", ids}});
        }
        return result;
    }
    using tab_table = std::unordered_map<std::string, std::vector<sm::object_id>>;
    tab_table tabs_from_json(const json& tabs_json, const sm::world& world) {
        tab_table tabs;
        for (const auto& json_pair : tabs_json) {
            auto& ids = tabs[json_pair.at("tab").get<std::string>()];
            for (const auto& skel_ref : json_pair.at("skeletons")) {
                auto str = skel_ref.get<std::string>();
                if (auto id = sm::object_id::from_string(str); id) {
                    if (!world.skeleton(*id)) {
                        throw std::runtime_error("tab references missing skeleton ID");
                    }
                    ids.push_back(*id);
                } else {
                    // Legacy project files stored skeleton names on tabs.
                    auto skel = world.skeleton(str);
                    if (!skel) {
                        throw std::runtime_error("tab references missing legacy skeleton name");
                    }
                    ids.push_back(skel->get().id());
                }
            }
        }
        return tabs;
    }
    std::optional<std::tuple<tab_table, sm::world>> json_to_project_components(
            const std::string& str) {
        try {
            json proj = json::parse(str);
            sm::world new_world;
            auto result = new_world.from_json(proj.at("world"));
            if (result != sm::result::success) {
                return {};
            }
            auto new_tabs = tabs_from_json(proj.at("tabs"), new_world);
            return {{std::move(new_tabs), std::move(new_world)}};
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

void mdl::project::delete_skeleton_from_canvas_table(
        const std::string& tab, const sm::object_id& skel) {
    auto iter_tab = tabs_.find(tab);
    if (iter_tab == tabs_.end()) {
        return;
    }
    auto& skel_group = iter_tab->second;
    auto iter_skel = r::find(skel_group, skel);
    if (iter_skel != skel_group.end()) {
        skel_group.erase(iter_skel);
    }
}
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
    tabs_.clear();
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
bool mdl::project::has_tab(const std::string& str) const { return tabs_.contains(str); }

std::span<const sm::object_id> mdl::project::skel_ids_on_tab(std::string_view name) const {
    return tabs_.at(std::string(name));
}
std::string mdl::project::to_json() const {
    json stick_man_project = {
        {"version", 1.0},
        {"tabs", tabs_to_json(tabs_)},
        {"world", world_.to_json()}
    };
    return stick_man_project.dump(4);
}

bool mdl::project::from_json(const std::string& str) {
    auto comps = json_to_project_components(str);
    if (!comps) {
        return false;
    }
    clear();
    tabs_ = std::move(std::get<0>(*comps));
    world_ = std::move(std::get<1>(*comps));
    advance_default_name_counters_from_world();
    emit new_project_opened(*this);
    return true;
}

bool mdl::project::add_new_tab(const std::string& tab_name) {
    if (has_tab(tab_name)) {
        return false;
    }
    execute_command(commands::make_add_tab_command(tab_name));
    return true;
}
void mdl::project::add_bone(const std::string& tab,
        const handle& u, const handle& v) {
    execute_command(commands::make_add_bone_command(tab, u, v, next_default_bone_name()));
}

void mdl::project::add_new_skeleton_root(const std::string& tab, sm::point loc) {
    execute_command(commands::make_create_node_command(tab, loc, next_default_node_name()));
}
std::string mdl::project::canvas_name_from_skeleton(const sm::object_id& skel) const {
    for (const auto& [canv_name, skels] : tabs_) {
        if (r::find(skels, skel) != skels.end()) {
            return canv_name;
        }
    }
    return {};
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

void mdl::project::replace_skeletons_aux(const std::string& canvas_name,
        const std::vector<sm::object_id>& replacees,
        const std::vector<sm::skel_ref>& replacements,
        std::vector<sm::object_id>* new_ids) {
    for (const auto& replacee : replacees) {
        if (canvas_name_from_skeleton(replacee) != canvas_name) {
            throw std::runtime_error("replace_skeletons must be called on a single canvas");
        }
        delete_skeleton_from_canvas_table(canvas_name, replacee);
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
        tabs_[canvas_name].push_back(new_skel->get().id());
    }

    advance_default_name_counters_from_world();
    emit refresh_canvas(*this, canvas_name, true);
}
void mdl::project::replace_skeletons(const std::string& canvas_name,
        const std::vector<sm::object_id>& replacees,
        const std::vector<sm::skel_ref>& replacements) {
    execute_command(commands::make_replace_skeletons_command(canvas_name, replacees, replacements));
}



bool mdl::identical_pieces(mdl::skel_piece p1, mdl::skel_piece p2) {
    return mdl::to_handle(p1) == mdl::to_handle(p2);
}
