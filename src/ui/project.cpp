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

    std::optional<std::tuple<ui::tab_table, sm::world>> json_to_project_components(const std::string& str) {
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

    std::vector<std::string> skeleton_named_on_canvas(const ui::canvas& canv) {
        auto skels = canv.skeleton_items();
        return ui::to_model_ptrs(rv::all(skels)) |
            rv::transform(
                [](sm::skeleton* skel)->std::string {
                    return skel->name();
                }
            ) | r::to<std::vector<std::string>>();
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

void ui::project::delete_skeleton_from_tab(const std::string& tab, const std::string& skel) {
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

    if (&u.owner().get() == &v.owner().get()) {
        return;
    }
    
    emit pre_new_bone_added(u, v);
    delete_skeleton_from_tab(tab, v.owner().get().name());

    auto bone = world_.create_bone({}, u, v);
    if (bone) {
        emit new_bone_added(bone->get());
    } else {
        throw std::runtime_error("ui::project::add_bone");
    }
}

void ui::project::add_new_skeleton_root(const std::string& tab, sm::point loc) {
    auto skel = world_.create_skeleton(loc);
    tabs_[tab].push_back(skel.get().name());
    emit new_skeleton_added(skel);
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

void ui::project::replace_skeletons(const std::string& canvas_name,
        const std::vector<std::string>& replacees,
        const std::vector<sm::skel_ref>& replacements) {
    //emit pre_refresh_canvas(canvas_name);

    for (auto replacee : replacees) {
        if (canvas_name_from_skeleton(replacee) != canvas_name) {
            throw std::runtime_error("replace_skeletons must be called on a single canvas");
        }
        delete_skeleton_from_tab(canvas_name, replacee);
        world_.delete_skeleton(replacee);

    }
    for (auto replacement : replacements) {
        auto& skel = replacement.get();
        auto new_skel = skel.copy_to(world_, unique_skeleton_name(skel.name(), world_.skeleton_names()));
        tabs_[canvas_name].push_back(new_skel->get().name());
    }

    emit refresh_canvas(*this, canvas_name);
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