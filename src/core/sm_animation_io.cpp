#include "sm_animation.hpp"
#include "json.hpp"
#include <stdexcept>
#include <cmath>

namespace {
using nlohmann::json;
using namespace sm;
object_id id(const json& j) {
    auto parsed = object_id::from_string(j.get<std::string>());
    if (!parsed) throw std::invalid_argument("Invalid animation ID");
    return *parsed;
}
json pt(point p) { return json::array({p.x, p.y}); }
point pt(const json& j) {
    point p{j.at(0).get<double>(), j.at(1).get<double>()};
    if (!std::isfinite(p.x) || !std::isfinite(p.y)) throw std::invalid_argument("Invalid path point");
    return p;
}
json ids(const std::vector<object_id>& list) { json j = json::array(); for (auto i : list) j.push_back(i.to_string()); return j; }
std::vector<object_id> ids(const json& j) { std::vector<object_id> v; for (const auto& i : j) v.push_back(id(i)); return v; }
template<class T> T enumeration(const json& j, int max) {
    int v = j.get<int>(); if (v < 0 || v > max) throw std::invalid_argument("Invalid animation enum"); return T(v);
}
json cubic(const cubic_bezier_path& p) { return json::array({pt(p.start), pt(p.control1), pt(p.control2), pt(p.end)}); }
cubic_bezier_path cubic(const json& j) { return {pt(j.at(0)), pt(j.at(1)), pt(j.at(2)), pt(j.at(3))}; }
json path_json(const target_path& path) {
    return std::visit([](const auto& p) -> json {
        using T = std::decay_t<decltype(p)>;
        if constexpr (std::is_same_v<T, line_path>) return {{"type", "line"}, {"points", {pt(p.start), pt(p.end)}}};
        else if constexpr (std::is_same_v<T, cubic_bezier_path>) return {{"type", "cubic"}, {"points", cubic(p)}};
        else { json segments = json::array(); for (const auto& s : p.segments) segments.push_back(cubic(s)); return {{"type", "spline"}, {"segments", segments}}; }
    }, path);
}
target_path read_path(const json& j) {
    auto type = j.at("type").get<std::string>();
    if (type == "line") return line_path{pt(j.at("points").at(0)), pt(j.at("points").at(1))};
    if (type == "cubic") return cubic(j.at("points"));
    if (type == "spline") { spline_path p; for (const auto& s : j.at("segments")) p.segments.push_back(cubic(s)); return p; }
    throw std::invalid_argument("Unknown path type");
}
json data_json(const action_data& data) {
    return std::visit([](const auto& d) -> json {
        using T = std::decay_t<decltype(d)>;
        if constexpr (std::is_same_v<T, rigid_rotation>) return {{"type", "rotation"}, {"bone", d.bone.to_string()}, {"pivot", int(d.pivot)}, {"angle", d.angle}};
        else if constexpr (std::is_same_v<T, rigid_translation>) return {{"type", "translation"}, {"skeletons", ids(d.skeletons)}, {"offset", pt(d.offset)}};
        else return {{"type", "ik_translation"}, {"effector", d.effector.to_string()}, {"pins", ids(d.pins)},
            {"reference", int(d.reference)}, {"reference_node", d.reference_node.to_string()}, {"path", path_json(d.path)}};
    }, data);
}
action_data read_data(const json& j) {
    auto type = j.at("type").get<std::string>();
    if (type == "rotation") return rigid_rotation{id(j.at("bone")), enumeration<rotation_pivot>(j.at("pivot"), 1), j.at("angle").get<double>()};
    if (type == "translation") return rigid_translation{ids(j.at("skeletons")), pt(j.at("offset"))};
    if (type == "ik_translation") return ik_translation{id(j.at("effector")), ids(j.at("pins")),
        enumeration<target_reference>(j.at("reference"), 2), id(j.at("reference_node")), read_path(j.at("path"))};
    throw std::invalid_argument("Unknown action type");
}
animation_time time(const json& j) {
    if (!j.is_number_integer() || (j.is_number_unsigned() && j.get<std::uint64_t>() > INT64_MAX))
        throw std::invalid_argument("Expected integer milliseconds");
    return j.get<animation_time>();
}
}
nlohmann::json sm::animation_assets_to_json(const animation_assets& assets) {
    assets.validate();
    json poses = json::array(), animations = json::array();
    for (const auto& p : assets.poses) {
        json positions = json::object();
        for (const auto& [i, pos] : p.node_positions) positions[i.to_string()] = pt(pos);
        poses.push_back({{"id", p.id.to_string()}, {"name", p.name}, {"nodes", positions}});
    }
    for (const auto& a : assets.animations) {
        json layers = json::array();
        for (const auto& layer : a.layers) {
            json actions = json::array();
            for (const auto& v : layer.actions) actions.push_back({{"id", v.id.to_string()}, {"start", v.start},
                {"duration", v.duration}, {"easing", int(v.easing)}, {"data", data_json(v.data)}});
            layers.push_back(actions);
        }
        animations.push_back({{"id", a.id.to_string()}, {"name", a.name}, {"base_pose", a.base_pose.to_string()}, {"layers", layers}});
    }
    return {{"root", assets.character_root.to_string()}, {"default_pose", assets.default_pose.to_string()}, {"poses", poses}, {"animations", animations}};
}
sm::animation_assets sm::animation_assets_from_json(const nlohmann::json& j) {
    animation_assets assets;
    assets.character_root = id(j.at("root")); assets.default_pose = id(j.at("default_pose"));
    for (const auto& v : j.at("poses")) {
        pose p; p.id = id(v.at("id")); p.name = v.at("name").get<std::string>();
        for (const auto& [key, value] : v.at("nodes").items()) p.node_positions.emplace(id(json(key)), pt(value));
        assets.poses.push_back(std::move(p));
    }
    for (const auto& v : j.at("animations")) {
        animation a; a.id = id(v.at("id")); a.name = v.at("name").get<std::string>(); a.base_pose = id(v.at("base_pose"));
        for (const auto& layer : v.at("layers")) {
            animation_layer l;
            for (const auto& action : layer) l.actions.push_back({id(action.at("id")), time(action.at("start")),
                time(action.at("duration")), enumeration<easing>(action.at("easing"), 4), read_data(action.at("data"))});
            a.layers.push_back(std::move(l));
        }
        assets.animations.push_back(std::move(a));
    }
    assets.validate(); return assets;
}
