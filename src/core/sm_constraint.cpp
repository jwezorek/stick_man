#include "sm_constraint.hpp"
#include "sm_project.hpp"
#include "sm_angle_set.hpp"
#include "json.hpp"
#include <set>
#include <cmath>
#include <numbers>

namespace sm {
bool constraint::references(object_id id) const {
    if (auto r = rotation()) return r->target_bone == id ||
        (r->reference.kind == rotation_reference_kind::bone && r->reference.bone_id == id);
    auto t = triangle();
    return t->first_bone == id || t->second_bone == id;
}
constraint constraint::remapped(const std::unordered_map<object_id, object_id>& ids) const {
    auto map = [&](object_id id) { auto it = ids.find(id); return it == ids.end() ? id : it->second; };
    auto def = definition_;
    if (auto r = std::get_if<rotation_constraint>(&def)) {
        r->target_bone = map(r->target_bone);
        if (r->reference.kind == rotation_reference_kind::bone) r->reference.bone_id = map(r->reference.bone_id);
    } else {
        auto& t = std::get<rigid_triangle_constraint>(def);
        t.first_bone = map(t.first_bone); t.second_bone = map(t.second_bone);
    }
    return {map(id_), name_, def};
}
result validate_constraints(const topology& topology, const constraint_map& constraints) {
    std::set<std::pair<object_id,object_id>> pairs;
    std::map<object_id, std::vector<std::pair<object_id,double>>> graph;
    for (auto& [id,c] : constraints) {
        if (id.is_nil() || id != c.id()) return result::invalid_constraint;
        if (topology.get<bone>(id) || topology.get<node>(id) || topology.contains_skeleton(id)) return result::duplicate_id;
        if (auto r = c.rotation()) {
            auto target = topology.get<bone>(r->target_bone);
            if (!target) return result::invalid_constraint;
            try { angle_set test(r->allowed); } catch (...) { return result::invalid_constraint; }
            switch (r->reference.kind) {
            case rotation_reference_kind::world: break;
            case rotation_reference_kind::parent:
                if (!target->get().parent_bone()) return result::no_parent;
                break;
            case rotation_reference_kind::bone:
                if (r->reference.bone_id == r->target_bone || !topology.get<bone>(r->reference.bone_id))
                    return result::invalid_constraint;
                break;
            default: return result::invalid_constraint;
            }
            if (r->reference.kind != rotation_reference_kind::bone && !r->reference.bone_id.is_nil())
                return result::invalid_constraint;
        } else {
            auto t = c.triangle();
            auto a = topology.get<bone>(t->first_bone), b = topology.get<bone>(t->second_bone);
            if (!a || !b || t->first_bone == t->second_bone || !std::isfinite(t->relative_angle) ||
                &a->get().parent_node() != &b->get().parent_node()) return result::invalid_constraint;
            auto pair = std::minmax(t->first_bone, t->second_bone);
            if (!pairs.emplace(pair.first,pair.second).second) return result::invalid_constraint;
            graph[t->first_bone].emplace_back(t->second_bone,t->relative_angle);
            graph[t->second_bone].emplace_back(t->first_bone,-t->relative_angle);
        }
    }
    std::map<object_id,double> offsets;
    for (auto& [start, edges] : graph) {
        if (offsets.contains(start)) continue;
        offsets[start] = 0;
        std::vector<object_id> stack{start};
        while (!stack.empty()) {
            auto a = stack.back(); stack.pop_back();
            for (auto [b, delta] : graph.at(a)) {
                auto proposed = normalize_angle(offsets.at(a) + delta);
                if (auto it = offsets.find(b); it != offsets.end()) {
                    if (std::abs(angular_distance(it->second,proposed)) > 1e-8)
                        return result::inconsistent_constraints;
                } else { offsets[b] = proposed; stack.push_back(b); }
            }
        }
    }
    return result::success;
}
nlohmann::json constraints_to_json(const constraint_map& constraints) {
    auto out = nlohmann::json::array();
    for (const auto& [id,c] : constraints) {
        nlohmann::json j{{"id", id.to_string()}, {"name", c.name()}};
        if (auto r = c.rotation()) {
            j["type"] = "rotation"; j["target_bone"] = r->target_bone.to_string();
            j["start_angle"] = r->allowed.start_angle; j["span_angle"] = r->allowed.span_angle;
            auto kind = r->reference.kind;
            j["reference"] = {{"type", kind == rotation_reference_kind::world ? "world" :
                kind == rotation_reference_kind::parent ? "parent" : "bone"}};
            if (kind == rotation_reference_kind::bone) j["reference"]["bone"] = r->reference.bone_id.to_string();
        } else {
            auto t = c.triangle(); j["type"] = "rigid_triangle";
            j["bone1"] = t->first_bone.to_string(); j["bone2"] = t->second_bone.to_string();
            j["relative_angle"] = t->relative_angle;
        }
        out.push_back(std::move(j));
    }
    return out;
}
constraint_map constraints_from_json(const nlohmann::json& j) {
    if (!j.is_array()) throw std::invalid_argument("constraints must be an array");
    auto id = [](const nlohmann::json& v) {
        auto parsed = object_id::from_string(v.get<std::string>());
        if (!parsed || parsed->is_nil()) throw std::invalid_argument("invalid constraint ID");
        return *parsed;
    };
    constraint_map out;
    for (auto& entry : j) {
        auto cid = id(entry.at("id"));
        constraint_definition def;
        if (entry.at("type") == "rotation") {
            auto ref = rotation_reference::world();
            auto& r = entry.at("reference");
            if (r.at("type") == "parent") ref = rotation_reference::parent();
            else if (r.at("type") == "bone") ref = rotation_reference::bone(id(r.at("bone")));
            else if (r.at("type") != "world") throw std::invalid_argument("unknown reference kind");
            def = rotation_constraint{id(entry.at("target_bone")), ref,
                {entry.at("start_angle").get<double>(),entry.at("span_angle").get<double>()}};
        } else if (entry.at("type") == "rigid_triangle")
            def = rigid_triangle_constraint{id(entry.at("bone1")),id(entry.at("bone2")),entry.at("relative_angle").get<double>()};
        else throw std::invalid_argument("unknown constraint kind");
        if (!out.emplace(cid, constraint(cid,entry.at("name").get<std::string>(),def)).second)
            throw std::invalid_argument("duplicate constraint ID");
    }
    return out;
}

static const constraint* editor_record(const bone& b) {
    for (auto& [id,c] : b.owner().owner().constraints())
        if (auto r = c.rotation(); r && r->target_bone == b.id() && r->reference.kind != rotation_reference_kind::bone) return &c;
    return nullptr;
}
std::optional<rot_constraint> editor_rotation_constraint(const bone& b) {
    auto c = editor_record(b);
    if (!c) return {};
    auto r = c->rotation();
    return rot_constraint{r->reference.kind == rotation_reference_kind::parent,r->allowed.start_angle,r->allowed.span_angle};
}
result set_editor_rotation_constraint(bone& b, double start, double span, bool parent) {
    auto p = b.owner().owner().owning_project();
    if (!p) return result::invalid_constraint;
    auto reference = parent ? rotation_reference::parent() : rotation_reference::world();
    if (auto c = editor_record(b)) return p->update_constraint(c->id(),rotation_constraint{b.id(),reference,{start,span}});
    auto added = p->add_rotation_constraint(b.id(),reference,{start,span});
    return added ? result::success : added.error();
}
void remove_editor_rotation_constraint(bone& b) {
    if (auto p = b.owner().owner().owning_project()) if (auto c = editor_record(b)) p->remove_constraint(c->id());
}
}
