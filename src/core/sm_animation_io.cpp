#include "sm_animation.hpp"
#include "json.hpp"
#include <cmath>
#include <stdexcept>

namespace {
using nlohmann::json;
using namespace sm;

object_id id(const json& j) {
    auto parsed = object_id::from_string(j.get<std::string>());
    if (!parsed) {
        throw std::invalid_argument("Invalid animation asset ID");
    }
    return *parsed;
}

json point_json(point p) {
    return json::array({p.x, p.y});
}

point read_point(const json& j) {
    point p{j.at(0).get<double>(), j.at(1).get<double>()};
    if (!std::isfinite(p.x) || !std::isfinite(p.y)) {
        throw std::invalid_argument("Invalid pose point");
    }
    return p;
}

json skeletal_pose_json(const skeletal_pose& p) {
    json roots = json::object();
    json bones = json::object();

    for (const auto& [i, pt] : p.root_positions) {
        roots[i.to_string()] = point_json(pt);
    }
    for (const auto& [i, angle] : p.bone_rotations) {
        bones[i.to_string()] = angle;
    }

    return {{"roots", roots}, {"bone_rotations", bones}};
}

skeletal_pose read_skeletal_pose(const json& j) {
    skeletal_pose p;
    for (const auto& [key, value] : j.at("roots").items()) {
        p.root_positions.emplace(id(json(key)), read_point(value));
    }
    for (const auto& [key, value] : j.at("bone_rotations").items()) {
        const double angle = value.get<double>();
        if (!std::isfinite(angle)) {
            throw std::invalid_argument("Invalid keyframe rotation");
        }
        p.bone_rotations.emplace(id(json(key)), angle);
    }
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
        for (const auto& [i, pos] : p.node_positions) {
            positions[i.to_string()] = point_json(pos);
        }
        result["poses"].push_back({
            {"id", p.id.to_string()},
            {"name", p.name},
            {"nodes", positions}
        });
    }

    if (!assets.animations.empty()) {
        result["animations"] = json::array();
        for (const auto& a : assets.animations) {
            json animation_json = {
                {"id", a.id.to_string()},
                {"name", a.name}
            };

            if (!a.keyframes.empty()) {
                animation_json["keyframes"] = json::array();
                for (const auto& keyframe : a.keyframes) {
                    json keyframe_json = {
                        {"id", keyframe.id.to_string()},
                        {"pose", skeletal_pose_json(keyframe.pose)}
                    };
                    if (keyframe.name) {
                        keyframe_json["name"] = *keyframe.name;
                    }
                    animation_json["keyframes"].push_back(std::move(keyframe_json));
                }

                animation_json["transitions"] = json::array();
                for (const auto& transition : a.transitions) {
                    animation_json["transitions"].push_back({
                        {"id", transition.id.to_string()},
                        {"duration_seconds", transition.duration_seconds}
                    });
                }
            }

            result["animations"].push_back(std::move(animation_json));
        }
    }

    return result;
}

sm::animation_assets sm::animation_assets_from_json(const nlohmann::json& j) {
    animation_assets assets;
    assets.default_pose = id(j.at("default_pose"));

    for (const auto& value : j.at("poses")) {
        pose p;
        p.id = id(value.at("id"));
        p.name = value.at("name").get<std::string>();
        for (const auto& [key, node_value] : value.at("nodes").items()) {
            p.node_positions.emplace(id(json(key)), read_point(node_value));
        }
        assets.poses.push_back(std::move(p));
    }

    if (j.contains("animations")) {
        for (const auto& value : j.at("animations")) {
            animation a;
            a.id = id(value.at("id"));
            a.name = value.at("name").get<std::string>();

            if (value.contains("keyframes")) {
                for (const auto& keyframe_value : value.at("keyframes")) {
                    pose_keyframe keyframe;
                    keyframe.id = id(keyframe_value.at("id"));
                    if (keyframe_value.contains("name")) {
                        keyframe.name = keyframe_value.at("name").get<std::string>();
                    }
                    keyframe.pose = read_skeletal_pose(keyframe_value.at("pose"));
                    a.keyframes.push_back(std::move(keyframe));
                }

                if (value.contains("transitions")) {
                    for (const auto& transition_value : value.at("transitions")) {
                        pose_transition transition;
                        transition.id = id(transition_value.at("id"));
                        transition.duration_seconds =
                            transition_value.at("duration_seconds").get<double>();
                        a.transitions.push_back(transition);
                    }
                } else {
                    a.reconcile_transitions();
                }
            }

            assets.animations.push_back(std::move(a));
        }
    }

    assets.validate();
    return assets;
}
