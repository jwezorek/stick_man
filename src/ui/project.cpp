#include "project.h"
#include "canvas.h"
#include "canvas_item.h"
#include "../core/sm_skeleton.h"
#include "../core/json.hpp"
#include <ranges>

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

    std::vector<std::string> skeleton_named_on_canvas(const ui::canvas& canv) {
        auto skels = canv.skeleton_items();
        return ui::to_model_ptrs(rv::all(skels)) |
            rv::transform(
                [](sm::skeleton* skel)->std::string {
                    return skel->name();
                }
            ) | r::to<std::vector<std::string>>();
    }
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

void ui::project::from_json(const std::string& str) {
}
