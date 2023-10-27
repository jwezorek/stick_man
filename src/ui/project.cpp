#include "project.h"
#include "canvas.h"
#include "canvas_item.h"
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

    std::optional<std::tuple<ui::tab_table, sm::world>> json_to_project_components(
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

namespace ui {
    class commands {
    public:

        struct skel_piece_handle {
            std::string skel_name;
            std::string piece_name;
        };

        template<sm::is_node_or_bone T>
        static std::expected<std::reference_wrapper<T>, sm::result> from_handle(
                ui::project& proj, const skel_piece_handle& hnd) {
            auto skel = proj.world_.skeleton(hnd.skel_name);
            if (!skel) {
                return std::unexpected(skel.error());
            }

            auto maybe_piece = skel->get().get_by_name<T>(hnd.piece_name);
            if (!maybe_piece) {
                return std::unexpected(sm::result::not_found);
            }
            return *maybe_piece;
        }

        static sm::expected_skel from_handle(ui::project& proj, const skel_piece_handle& hnd) {
            return proj.world_.skeleton(hnd.skel_name);
        }

        static skel_piece_handle to_handle( sm::is_node_or_bone auto& piece) {
            auto skel_name = piece.owner().get().name();
            return { skel_name, piece.name() };
        }

        static skel_piece_handle to_handle(const ui::skel_piece& piece) {
            return std::visit(
                overload{
                    [](sm::is_node_or_bone_ref auto node_or_bone)->skel_piece_handle {
                        return to_handle(node_or_bone.get());
                    } ,
                    [](sm::skel_ref itm)->skel_piece_handle {
                        auto& skel = itm.get();
                        return {skel.name(), {}};
                    }
                },
                piece
            );
        }

        struct create_node_state {
            std::string tab_name;
            std::string skeleton;
            sm::point loc;
        };

        static ui::command make_create_node_command(const std::string& tab, const sm::point& pt) {
            auto state = std::make_shared<create_node_state>(tab, "", pt);

            return {
                [state](ui::project& proj) {
                    auto skel = proj.world_.create_skeleton(state->loc);
                    state->skeleton = skel.get().name();
                    proj.tabs_[state->tab_name].push_back(skel.get().name());
                    emit proj.new_skeleton_added(skel);
                },
                [state](ui::project& proj) {
                    proj.delete_skeleton_name_from_canvas_table(
                        state->tab_name, state->skeleton
                    );
                    proj.world_.delete_skeleton(state->skeleton);
                    emit proj.refresh_canvas(proj, state->tab_name, true);
                }
            };
        }

        struct add_node_state {
            std::string canvas_name;
            skel_piece_handle u_hnd;
            skel_piece_handle v_hnd;
            sm::world original;
            std::string merged;

            add_node_state(const std::string& cn, sm::node& u, sm::node& v) :
                canvas_name(cn), u_hnd(to_handle(u)), v_hnd(to_handle(v))
            {}
        };

        static ui::command make_add_bone_command(const std::string& tab, sm::node& u, sm::node& v) {
            auto state = std::make_shared<add_node_state>(tab, u, v);
            return {
                [state](ui::project& proj) {
                    auto& u = from_handle<sm::node>(proj, state->u_hnd)->get();
                    auto& v = from_handle<sm::node>(proj, state->v_hnd)->get();

                    if (&u.owner().get() == &v.owner().get()) {
                        return;
                    }

                    auto& skel_u = u.owner().get();
                    auto& skel_v = v.owner().get();
                    auto new_u = skel_u.copy_to(state->original);
                    auto new_v = skel_v.copy_to(state->original);

                    if (!new_u || !new_v) {
                        throw std::runtime_error("ui::command make_add_bone_command");
                    }

                    emit proj.pre_new_bone_added(u, v);
                    proj.delete_skeleton_name_from_canvas_table(
                        state->canvas_name, v.owner().get().name()
                    );

                    auto bone = proj.world_.create_bone({}, u, v);
                    if (bone) {
                        state->merged = bone->get().owner().get().name();
                        emit proj.new_bone_added(bone->get());
                    } else {
                        throw std::runtime_error("ui::project::add_bone");
                    }
                },
                [state](ui::project& proj) {
                    proj.replace_skeletons(state->canvas_name,
                        {state->merged},
                        state->original.skeletons() | r::to<std::vector<sm::skel_ref>>(),
                        false
                    );
                    state->original.clear();
                }
            };
        }
    };
}

void ui::project::delete_skeleton_name_from_canvas_table(
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

void ui::project::clear_redo_stack() {
    redo_stack_ = {};
}

void ui::project::execute_command(const command& cmd) {
    clear_redo_stack();
    cmd.redo(*this);
    undo_stack_.push(cmd);
    emit refresh_undo_redo_state(can_redo(), can_undo());
}

ui::project::project()
{}

const sm::world& ui::project::world() const {
    return world_;
}

sm::world& ui::project::world() {
    return world_;
}

void ui::project::clear() {
    tabs_.clear();
    world_.clear();
    redo_stack_ = {};
    undo_stack_ = {};
}

void ui::project::undo() {
    if (!can_undo()) {
        return;
    }
    auto cmd = undo_stack_.top();
    undo_stack_.pop();
    cmd.undo(*this);
    redo_stack_.push(cmd);

    emit refresh_undo_redo_state(can_redo(), can_undo());
}

void ui::project::redo() {
    if (!can_redo()) {
        return;
    }
    auto cmd = redo_stack_.top();
    redo_stack_.pop();
    cmd.redo(*this);
    undo_stack_.push(cmd);

    emit refresh_undo_redo_state(can_redo(), can_undo());
}

bool ui::project::can_undo() const {
    return !undo_stack_.empty();
}

bool ui::project::can_redo() const {
    return !redo_stack_.empty();
}

bool ui::project::has_tab(const std::string& str) const {
    return tabs_.contains(str);
}

bool ui::project::add_new_tab(const std::string& name)
{
    if (has_tab(name)) {
        return false;
    }
    tabs_[name] = {};
    emit new_tab_added(name);
    return true;
}

std::span<const std::string> ui::project::skel_names_on_tab(std::string_view name) const {
    return tabs_.at(std::string(name));
}

std::string ui::project::to_json() const {
    json stick_man_project = {
        {"version", 0.0},
        {"tabs", tabs_to_json(tabs_)},
        {"world", world_.to_json()}
    };

    return stick_man_project.dump(4);
}

bool ui::project::from_json(const std::string& str) {

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

void ui::project::add_bone(const std::string& tab, sm::node& u, sm::node& v) {

    execute_command(
        commands::make_add_bone_command(tab, u, v)
    );
}

void ui::project::add_new_skeleton_root(const std::string& tab, sm::point loc) {
    execute_command(
        commands::make_create_node_command(tab, loc)
    );
}

//TODO: maintain a skeleton -> canvas table...
std::string ui::project::canvas_name_from_skeleton(const std::string& skel) const {
    for (const auto& [canv_name, skels] : tabs_) {
        for (auto skel_name : skels) {
            if (skel == skel_name) {
                return canv_name;
            }
        }
    }
    return {};
}

bool ui::project::rename_aux(skel_piece piece_var, const std::string& new_name)
{
    auto result = std::visit(
        [&](auto ref)->sm::result {
            auto& piece = ref.get();
            auto& owner = piece.owner().get();
            return owner.set_name(piece, new_name);
        },
        piece_var
    );
    if (result == sm::result::success) {
        emit name_changed(piece_var, new_name);
        return true;
    }
    return false;
}

bool ui::project::rename(skel_piece piece, const std::string& new_name) {
    return false;
}

void ui::project::transform(const std::vector<sm::node_ref>& nodes,
        const std::function<void(sm::node&)>& fn) {
    if (nodes.empty()) {
        return;
    }
    for (auto node : nodes) {
        fn(node.get());
    }
    auto canv = canvas_name_from_skeleton(
        nodes.front().get().owner().get().name()
    );
    emit refresh_canvas(*this, canv, false);
}

void ui::project::transform(const std::vector<sm::bone_ref>& bones,
        const std::function<void(sm::bone&)>& fn) {
    if (bones.empty()) {
        return;
    }
    for (auto bone : bones) {
        fn(bone.get());
    }
    auto canv = canvas_name_from_skeleton(
        bones.front().get().owner().get().name()
    );
    emit refresh_canvas(*this, canv, false);
}

void ui::project::replace_skeletons(const std::string& canvas_name,
        const std::vector<std::string>& replacees,
        const std::vector<sm::skel_ref>& replacements,
        bool should_rename) {

    for (auto replacee : replacees) {
        if (canvas_name_from_skeleton(replacee) != canvas_name) {
            throw std::runtime_error("replace_skeletons must be called on a single canvas");
        }
        delete_skeleton_name_from_canvas_table(canvas_name, replacee);
        world_.delete_skeleton(replacee);

    }
    for (auto replacement : replacements) {
        auto& skel = replacement.get();
        auto new_skel = skel.copy_to(
            world_, 
            should_rename ? unique_skeleton_name(skel.name(), world_.skeleton_names()) : ""
        );
        if (!new_skel) {
            throw std::runtime_error("ui::project::replace_skeletons");
        }
        tabs_[canvas_name].push_back(new_skel->get().name());
    }

    emit refresh_canvas(*this, canvas_name, true);
}

std::string ui::unique_skeleton_name(const std::string& old_name,
    const std::vector<std::string>& used_names) {
    auto is_not_unique = r::find_if(used_names,
        [old_name](auto&& str) {return str == old_name; }
    ) != used_names.end();
    return (is_not_unique) ?
        ui::make_unique_name(used_names, get_prefix(old_name)) :
        old_name;
}

bool ui::identical_pieces(ui::skel_piece p1, ui::skel_piece p2) {
    auto to_void_star = [](ui::skel_piece sp) {
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