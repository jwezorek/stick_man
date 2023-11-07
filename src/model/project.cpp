#include "project.h"
#include "../ui/canvas.h"
#include "../ui/canvas_item.h"
#include "commands.h"
#include "../core/sm_skeleton.h"
#include "../core/json.hpp"
#include <ranges>
#include <optional>
#include <tuple>

using json = nlohmann::json;
namespace r = std::ranges;
namespace rv = std::ranges::views;

/*------------------------------------------------------------------------------------------------*/

namespace {

    template<class... Ts> struct overload : Ts... { using Ts::operator()...; };

    json tabs_to_json(const std::unordered_map<std::string, std::vector<std::string>>& tabs) {
        return tabs |
            rv::transform(
                [](const auto& item)->json {
                    const auto& [key, val] = item;
                    json json_pair = {
                        {"tab", key},
                        {"skeletons",  rv::all(val) | r::to<json>()}
                    };
                    return json_pair;
                }
        ) | r::to<json>();
    }

    std::unordered_map<std::string, std::vector<std::string>> tabs_from_json(const json& tabs_json) {
        std::unordered_map<std::string, std::vector<std::string>> tabs;
        for (const auto json_pair : tabs_json) {
            tabs[json_pair["tab"]] = json_pair["skeletons"] | r::to<std::vector<std::string>>();
        }
        return tabs;
    }

    using tab_table = std::unordered_map<std::string, std::vector<std::string>>;

    std::optional<std::tuple<tab_table, sm::world>> json_to_project_components(
            const std::string& str) {
        try {

            json proj = json::parse(str);
            auto new_tabs = tabs_from_json(proj["tabs"]);
            sm::world new_world;
            auto result = new_world.from_json(proj["world"]);

            if (result != sm::result::success) {
                throw result;
            }

            return { {new_tabs, std::move(new_world) } };

        } catch (...) {
            return {};
        };
    }

    std::string get_prefix(const std::string& name) {
        if (!name.contains('-')) {
            return name;
        }
        int n = static_cast<int>(name.size());
        int i;
        bool at_least_one_digit = false;
        for (i = n - 1; i >= 0; --i) {
            if (!std::isdigit(name[i])) {
                break;
            }
            else {
                at_least_one_digit = true;
            }
        }
        if (!at_least_one_digit || i < 0) {
            return name;
        }
        if (name[i] != '-') {
            return name;
        }
        return name.substr(0, i);
    }
}

/*------------------------------------------------------------------------------------------------*/

void mdl::project::delete_skeleton_name_from_canvas_table(
        const std::string& tab, const std::string& skel) {
    auto iter_tab = tabs_.find(tab);
    if (iter_tab == tabs_.end()) {
        return;
    }
    auto& skel_group = iter_tab->second;
    auto iter_skel = r::find_if(skel_group, [skel](auto str) {return str == skel; });
    if (iter_skel == skel_group.end()) {
        return;
    }
    skel_group.erase(iter_skel);
}

void mdl::project::clear_redo_stack() {
    redo_stack_ = {};
}

void mdl::project::execute_command(const command& cmd) {
    clear_redo_stack();
    cmd.redo(*this);
    undo_stack_.push(cmd);
    emit refresh_undo_redo_state(can_redo(), can_undo());
}

mdl::project::project()
{}

const sm::world& mdl::project::world() const {
    return world_;
}

sm::world& mdl::project::world() {
    return world_;
}

void mdl::project::clear() {
    tabs_.clear();
    world_.clear();
    redo_stack_ = {};
    undo_stack_ = {};
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

bool mdl::project::can_undo() const {
    return !undo_stack_.empty();
}

bool mdl::project::can_redo() const {
    return !redo_stack_.empty();
}

bool mdl::project::has_tab(const std::string& str) const {
    return tabs_.contains(str);
}

std::span<const std::string> mdl::project::skel_names_on_tab(std::string_view name) const {
    return tabs_.at(std::string(name));
}

std::string mdl::project::to_json() const {
    json stick_man_project = {
        {"version", 0.0},
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
    tabs_ = std::get<0>(*comps);
    world_ = std::move(std::get<1>(*comps));

    emit new_project_opened(*this);
    return true;
}

bool mdl::project::add_new_tab(const std::string& tab_name)
{
    if (has_tab(tab_name)) {
        return false;
    }
    execute_command(
        commands::make_add_tab_command(tab_name)
    );
    return true;
}

void mdl::project::add_bone(const std::string& tab, 
        const handle& u, const handle& v) {
    execute_command(
        commands::make_add_bone_command(tab, u, v)
    );
}

void mdl::project::add_new_skeleton_root(const std::string& tab, sm::point loc) {
    execute_command(
        commands::make_create_node_command(tab, loc)
    );
}

//TODO: maintain a skeleton -> canvas table...
std::string mdl::project::canvas_name_from_skeleton(const std::string& skel) const {
    for (const auto& [canv_name, skels] : tabs_) {
        for (auto skel_name : skels) {
            if (skel == skel_name) {
                return canv_name;
            }
        }
    }
    return {};
}

void mdl::project::rename_aux(skel_piece piece_var, const std::string& new_name)
{
    auto result = std::visit(
        [&](auto ref)->sm::result {
            auto& piece = ref.get();
            auto& owner = piece.owner().get();
            return owner.set_name(piece, new_name);
        },
        piece_var
    );
    if (result != sm::result::success) {
        throw std::runtime_error("mod::project::rename_aux");
    }
    
    emit name_changed(piece_var, new_name);
}

bool mdl::project::can_rename(skel_piece piece, const std::string& new_name) {
    return std::visit(
        overload{
            [new_name](sm::is_node_or_bone_ref auto node_or_bone_ref)->bool {
                const auto& node_or_bone = node_or_bone_ref.get();
                using value_type = std::remove_cvref_t<decltype(node_or_bone)>;
                const auto& skel = node_or_bone.owner().get();
                return !skel.contains<value_type>(new_name);
            } ,
            [new_name](sm::skel_ref itm)->bool {
                const auto& skel = itm.get();
                const auto& world = skel.owner().get();
                return !world.contains_skeleton(new_name);
            }
        },
        piece
    );
}

bool mdl::project::rename(skel_piece piece, const std::string& new_name) {
    if (!can_rename(piece, new_name)) {
        return false;
    }

    std::visit(
        [this, piece, new_name](auto ref) {
            using value_type = std::remove_cvref_t<decltype(ref.get())>;
            execute_command(
                commands::make_rename_command<value_type>(ref, new_name)
            );
        },
        piece
    );

    return true;
}

void mdl::project::transform(const std::vector<handle>& nodes,
        const std::function<void(sm::node&)>& fn) {
    execute_command(
        commands::make_transform_bones_or_nodes_command(*this, nodes, {}, fn, {})
    );
}

void mdl::project::transform(const std::vector<handle>& bones,
        const std::function<void(sm::bone&)>& fn) {
    execute_command(
        commands::make_transform_bones_or_nodes_command(*this, {}, bones, {}, fn)
    );
}

void mdl::project::replace_skeletons_aux(const std::string& canvas_name,
        const std::vector<std::string>& replacees,
        const std::vector<sm::skel_ref>& replacements,
        std::vector<std::string>* new_names) {

    for (auto replacee : replacees) {
        if (canvas_name_from_skeleton(replacee) != canvas_name) {
            throw std::runtime_error("replace_skeletons must be called on a single canvas");
        }
        delete_skeleton_name_from_canvas_table(canvas_name, replacee);
        world_.delete_skeleton(replacee);

    }
    bool should_rename = new_names != nullptr;
    for (auto replacement : replacements) {
        auto& skel = replacement.get();
        auto new_skel = skel.copy_to(
            world_, 
            should_rename ? unique_skeleton_name(skel.name(), world_.skeleton_names()) : ""
        );
        if (!new_skel) {
            throw std::runtime_error("skeleton copy failed");
        }
        if (should_rename) {
            new_names->push_back(new_skel->get().name());
        }
        tabs_[canvas_name].push_back(new_skel->get().name());
    }

    emit refresh_canvas(*this, canvas_name, true);
}

void mdl::project::replace_skeletons(const std::string& canvas_name,
        const std::vector<std::string>& replacees,
        const std::vector<sm::skel_ref>& replacements) {
    execute_command(
        commands::make_replace_skeletons_command(canvas_name, replacees, replacements)
    );
}

std::string mdl::unique_skeleton_name(const std::string& old_name,
    const std::vector<std::string>& used_names) {
    auto is_not_unique = r::find_if(used_names,
        [old_name](auto&& str) {return str == old_name; }
    ) != used_names.end();
    return (is_not_unique) ?
        ui::make_unique_name(used_names, get_prefix(old_name)) :
        old_name;
}

bool mdl::identical_pieces(mdl::skel_piece p1, mdl::skel_piece p2) {
    auto to_void_star = [](mdl::skel_piece sp) {
        return std::visit(
            [](auto& p)->void* {
                return &p.get();
            },
            sp
        );
        };
    auto ptr1 = to_void_star(p1);
    auto ptr2 = to_void_star(p2);

    return ptr1 == ptr2;
}

