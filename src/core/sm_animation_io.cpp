#include "sm_animation.hpp"
#include "json.hpp"
#include <stdexcept>
#include <cmath>
#include <string_view>
#include <type_traits>

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
json path_json(const motion_path& path) {
    return std::visit([](const auto& p) -> json {
        using T = std::decay_t<decltype(p)>;
        if constexpr (std::is_same_v<T, line_path>) return {{"type", "line"}, {"points", {pt(p.start), pt(p.end)}}};
        else if constexpr (std::is_same_v<T, cubic_bezier_path>) return {{"type", "cubic"}, {"points", cubic(p)}};
        else { json segments = json::array(); for (const auto& s : p.segments) segments.push_back(cubic(s)); return {{"type", "spline"}, {"segments", segments}}; }
    }, path.geometry());
}
motion_path read_path(const json& j) {
    auto type = j.at("type").get<std::string>();
    if (type == "line") return motion_path(line_path{pt(j.at("points").at(0)), pt(j.at("points").at(1))});
    if (type == "cubic") return motion_path(cubic(j.at("points")));
    if (type == "spline") { spline_path p; for (const auto& s : j.at("segments")) p.segments.push_back(cubic(s)); return motion_path(std::move(p)); }
    throw std::invalid_argument("Unknown path type");
}
std::string reference_name(translation_reference reference) {
    switch(reference) {
    case translation_reference::animation_root:return "animation_root";
    case translation_reference::character_root:return "character_root";
    case translation_reference::bone:return "bone";
    }
    throw std::invalid_argument("Invalid translation reference");
}
translation_reference read_reference(const json& j) {
    if(j.is_number_integer()) { // Compatibility with the pre-translation-authoring draft format.
        int v=j.get<int>();
        if(v==0) return translation_reference::animation_root;
        if(v==1) return translation_reference::character_root;
        throw std::invalid_argument("Legacy node-relative translation paths cannot be converted to bone-relative paths");
    }
    auto value=j.get<std::string>();
    if(value=="animation_root") return translation_reference::animation_root;
    if(value=="character_root") return translation_reference::character_root;
    if(value=="bone") return translation_reference::bone;
    throw std::invalid_argument("Unknown translation reference");
}
json translation_common(const motion_path& path,translation_reference reference,object_id reference_bone) {
    json j={{"path",path_json(path)},{"reference",reference_name(reference)}};
    if(reference==translation_reference::bone) j["reference_bone"]=reference_bone.to_string();
    return j;
}

// Persistence is intentionally keyed from the action_data alternatives rather
// than from a separate hand-maintained type switch. The recursive reader below
// instantiates this codec for every variant alternative, so adding an action
// without defining both its read and write behavior fails to compile.
template<class Action>
struct action_json_codec;

template<>
struct action_json_codec<rigid_rotation> {
    static constexpr std::string_view tag = "rotation";
    static json write(const rigid_rotation& d) {
        return {{"type", std::string(tag)}, {"bone", d.bone.to_string()}, {"pivot", int(d.pivot)},
            {"propagation", int(d.propagation)}, {"angle", d.angle}};
    }
    static rigid_rotation read(const json& j) {
        const auto propagation = j.contains("propagation") ?
            enumeration<rotation_propagation>(j.at("propagation"), 1) : rotation_propagation::hierarchy;
        return {id(j.at("bone")), enumeration<rotation_pivot>(j.at("pivot"), 1),
            j.at("angle").get<double>(), propagation};
    }
};

template<>
struct action_json_codec<ik_rotation> {
    static constexpr std::string_view tag = "ik_rotation";
    static json write(const ik_rotation& d) {
        return {{"type", std::string(tag)}, {"effector", d.effector.to_string()},
            {"pivot_node", d.pivot_node.to_string()}, {"angle", d.angle}};
    }
    static ik_rotation read(const json& j) {
        return {id(j.at("effector")), id(j.at("pivot_node")), j.at("angle").get<double>()};
    }
};

template<>
struct action_json_codec<rigid_translation> {
    static constexpr std::string_view tag = "translation";
    static json write(const rigid_translation& d) {
        auto j=translation_common(d.path,d.reference,d.reference_bone);
        j["type"]=std::string(tag); j["skeletons"]=ids(d.skeletons); return j;
    }
    static rigid_translation read(const json& j) {
        if(j.contains("offset")) return {ids(j.at("skeletons")),
            motion_path(line_path{{0,0},pt(j.at("offset"))}),translation_reference::animation_root,{}};
        auto reference=read_reference(j.at("reference"));
        object_id bone{}; if(reference==translation_reference::bone) bone=id(j.at("reference_bone"));
        return {ids(j.at("skeletons")),read_path(j.at("path")),reference,bone};
    }
};

template<>
struct action_json_codec<ik_translation> {
    static constexpr std::string_view tag = "ik_translation";
    static json write(const ik_translation& d) {
        auto j=translation_common(d.path,d.reference,d.reference_bone);
        j["type"]=std::string(tag); j["effector"]=d.effector.to_string(); j["pins"]=ids(d.pins); return j;
    }
    static ik_translation read(const json& j) {
        auto reference=read_reference(j.at("reference"));
        object_id bone{}; if(reference==translation_reference::bone) bone=id(j.at("reference_bone"));
        return {id(j.at("effector")),ids(j.at("pins")),read_path(j.at("path")),reference,bone};
    }
};

json data_json(const action_data& data) {
    return std::visit([](const auto& d) -> json {
        return action_json_codec<std::decay_t<decltype(d)>>::write(d);
    }, data);
}

template<std::size_t I = 0>
action_data read_data_for_type(std::string_view type, const json& j) {
    if constexpr (I == std::variant_size_v<action_data>) {
        throw std::invalid_argument("Unknown action type");
    } else {
        using action_type = std::variant_alternative_t<I, action_data>;
        if (type == action_json_codec<action_type>::tag)
            return action_json_codec<action_type>::read(j);
        return read_data_for_type<I + 1>(type, j);
    }
}

action_data read_data(const json& j) {
    return read_data_for_type(j.at("type").get<std::string>(), j);
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
    return {{"default_pose", assets.default_pose.to_string()}, {"poses", poses}, {"animations", animations}};
}
sm::animation_assets sm::animation_assets_from_json(const nlohmann::json& j) {
    animation_assets assets;
    // v5 and earlier stored a character-root node here. Character root is now a
    // character-level bone property, so the legacy field is intentionally ignored.
    assets.default_pose = id(j.at("default_pose"));
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
