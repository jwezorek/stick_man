#include "sm_animation.hpp"
#include "json.hpp"
#include <cmath>
#include <stdexcept>

namespace {
using nlohmann::json;
using namespace sm;

object_id id(const json& j) {
    auto parsed = object_id::from_string(j.get<std::string>());
    if (!parsed) throw std::invalid_argument("Invalid animation asset ID");
    return *parsed;
}

json point_json(point p) { return json::array({p.x, p.y}); }
point read_point(const json& j) {
    point p{j.at(0).get<double>(), j.at(1).get<double>()};
    if (!std::isfinite(p.x) || !std::isfinite(p.y))
        throw std::invalid_argument("Invalid pose point");
    return p;
}
}

nlohmann::json sm::animation_assets_to_json(const animation_assets& assets) {
    assets.validate();
    json result;
    result["default_pose"] = assets.default_pose.to_string();
    result["poses"] = json::array();

    for (const auto& p : assets.poses) {
        json positions = json::object();
        for (const auto& [i, pos] : p.node_positions) positions[i.to_string()] = point_json(pos);
        result["poses"].push_back({{"id", p.id.to_string()}, {"name", p.name}, {"nodes", positions}});
    }

    // An empty animation collection carries no information in V2 Phase 1.
    if (!assets.animations.empty()) {
        result["animations"] = json::array();
        for (const auto& a : assets.animations)
            result["animations"].push_back({{"id", a.id.to_string()}, {"name", a.name}});
    }
    return result;
}

sm::animation_assets sm::animation_assets_from_json(const nlohmann::json& j) {
    animation_assets assets;
    assets.default_pose = id(j.at("default_pose"));
    for (const auto& v : j.at("poses")) {
        pose p;
        p.id = id(v.at("id"));
        p.name = v.at("name").get<std::string>();
        for (const auto& [key, value] : v.at("nodes").items())
            p.node_positions.emplace(id(json(key)), read_point(value));
        assets.poses.push_back(std::move(p));
    }
    if (j.contains("animations")) {
        for (const auto& v : j.at("animations")) {
            animation a;
            a.id = id(v.at("id"));
            a.name = v.at("name").get<std::string>();
            assets.animations.push_back(std::move(a));
        }
    }
    assets.validate();
    return assets;
}
