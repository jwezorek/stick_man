#include "sm_animation.hpp"
#include "sm_geometry_batch.hpp"
#include "sm_skeleton.hpp"
#include <algorithm>
#include <cmath>
#include <functional>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace {
void add_asset_id(std::unordered_set<sm::object_id>& ids, sm::object_id id) {
    if (id.is_nil() || !ids.insert(id).second) {
        throw std::invalid_argument("Duplicate animation asset ID");
    }
}

// Shared local checks for project/load validation and standalone sampling.
// The caller supplies the ID scope (all assets, or just this animation).
void validate_animation(const sm::animation& a, std::unordered_set<sm::object_id>& ids) {
    add_asset_id(ids, a.id);
    const auto wanted = a.keyframes.empty() ? std::size_t{} : a.keyframes.size() - 1;
    if (a.transitions.size() != wanted) {
        throw std::invalid_argument("Animation transition count does not match keyframes");
    }
    for (const auto& keyframe : a.keyframes) {
        add_asset_id(ids, keyframe.id);
        for (const auto& [id, pt] : keyframe.pose.root_positions) {
            if (id.is_nil() || !std::isfinite(pt.x) || !std::isfinite(pt.y)) {
                throw std::invalid_argument("Invalid keyframe root position");
            }
        }
        for (const auto& [id, angle] : keyframe.pose.bone_rotations) {
            if (id.is_nil() || !std::isfinite(angle)) {
                throw std::invalid_argument("Invalid keyframe rotation");
            }
        }
    }
    for (const auto& transition : a.transitions) {
        add_asset_id(ids, transition.id);
        if (!(transition.duration_seconds > 0.0) ||
                !std::isfinite(transition.duration_seconds)) {
            throw std::invalid_argument("Invalid transition duration");
        }
        for (const auto& [node_id, path] : transition.paths) {
            if (node_id.is_nil() || path.node != node_id ||
                (path.shape != sm::path_shape::line && path.shape != sm::path_shape::cubic &&
                 path.shape != sm::path_shape::spline) ||
                (path.shape == sm::path_shape::cubic && path.knots.size()!=2) ||
                (path.shape == sm::path_shape::spline && path.knots.size()<2))
                throw std::invalid_argument("Invalid animation path");
            for (const auto& knot : path.knots) {
                for (auto pt : {knot.position, knot.handle_in, knot.handle_out})
                    if (!std::isfinite(pt.x) || !std::isfinite(pt.y))
                        throw std::invalid_argument("Non-finite animation path geometry");
            }
        }
        for (const auto id : transition.pinned_nodes) {
            if (id.is_nil())
                throw std::invalid_argument("Invalid transition pin");
        }
        for (const auto& [cid, c] : transition.rotation_constraints) {
            if (cid.is_nil() || cid != c.id() || !c.rotation())
                throw std::invalid_argument("Invalid transition rotation constraint");
            add_asset_id(ids, cid);
        }
    }
    if (!std::isfinite(a.duration_seconds())) {
        throw std::invalid_argument("Animation total duration is not finite");
    }
}

bool same_ids(const auto& a, const auto& b) {
    return a.size() == b.size() && std::all_of(a.begin(), a.end(),
        [&](const auto& entry) { return b.contains(entry.first); });
}
}

const sm::pose* sm::animation_assets::find_pose(object_id id) const {
    for (const auto& p : poses) {
        if (p.id == id)
            return &p;
    }
    return nullptr;
}

const sm::animation* sm::animation_assets::find_animation(object_id id) const {
    for (const auto& a : animations) {
        if (a.id == id)
            return &a;
    }
    return nullptr;
}

sm::animation* sm::animation_assets::find_animation(object_id id) {
    for (auto& a : animations) {
        if (a.id == id)
            return &a;
    }
    return nullptr;
}

const sm::pose_keyframe* sm::animation::find_keyframe(object_id id) const {
    for (const auto& keyframe : keyframes) {
        if (keyframe.id == id)
            return &keyframe;
    }
    return nullptr;
}

sm::pose_keyframe* sm::animation::find_keyframe(object_id id) {
    for (auto& keyframe : keyframes) {
        if (keyframe.id == id)
            return &keyframe;
    }
    return nullptr;
}

std::optional<std::size_t> sm::animation::keyframe_index(object_id id) const {
    for (std::size_t i = 0; i < keyframes.size(); ++i) {
        if (keyframes[i].id == id)
            return i;
    }
    return {};
}

void sm::animation::reconcile_transitions() {
    const auto wanted = keyframes.empty() ? std::size_t{} : keyframes.size() - 1;
    while (transitions.size() < wanted) {
        transitions.emplace_back();
    }
    transitions.resize(wanted);
}

double sm::animation::duration_seconds() const {
    double result = 0;
    for (const auto& transition : transitions) result += transition.duration_seconds;
    return result;
}

std::optional<sm::reference_pose_sample> sm::sample_reference_pose(
        const animation& animation, double time_seconds) {
    if (!std::isfinite(time_seconds)) {
        throw std::invalid_argument("Reference pose time is not finite");
    }
    std::unordered_set<object_id> ids;
    validate_animation(animation, ids);
    if (animation.keyframes.empty())
        return {};

    const auto& first = animation.keyframes.front().pose;
    for (const auto& keyframe : animation.keyframes) {
        if (!same_ids(first.root_positions, keyframe.pose.root_positions) ||
                !same_ids(first.bone_rotations, keyframe.pose.bone_rotations)) {
            throw std::invalid_argument("Reference pose keyframe ID sets do not match");
        }
    }

    const auto exact = [&](std::size_t index) -> reference_pose_sample {
        const auto& keyframe = animation.keyframes[index];
        return {keyframe.pose, reference_keyframe{keyframe.id}};
    };
    const double time = std::clamp(time_seconds, 0.0, animation.duration_seconds());
    double start = 0;
    for (std::size_t i = 0; i < animation.transitions.size(); ++i) {
        const auto& transition = animation.transitions[i];
        const double end = start + transition.duration_seconds;
        if (time < end) {
            if (time == start)
                return exact(i);
            const auto& from = animation.keyframes[i];
            const auto& to = animation.keyframes[i + 1];
            const double u = (time - start) / transition.duration_seconds;
            skeletal_pose pose;
            for (const auto& [id, position] : from.pose.root_positions) {
                const auto& target = to.pose.root_positions.at(id);
                // std::lerp avoids overflow in target - position for finite endpoints.
                pose.root_positions.emplace(id, point{
                    std::lerp(position.x, target.x, u), std::lerp(position.y, target.y, u)});
            }
            for (const auto& [id, angle] : from.pose.bone_rotations) {
                // Normalize before subtraction so even extreme finite angles cannot
                // overflow angular_distance. Exact endpoints never take this path.
                const double source = normalize_angle(angle);
                const double target = normalize_angle(to.pose.bone_rotations.at(id));
                pose.bone_rotations.emplace(id,
                    normalize_angle(source + u * angular_distance(source, target)));
            }
            return reference_pose_sample{std::move(pose), reference_transition{
                transition.id, from.id, to.id, u}};
        }
        start = end;
    }
    return exact(animation.keyframes.size() - 1);
}

void sm::animation_assets::validate() const {
    if (!find_pose(default_pose)) {
        throw std::invalid_argument("Missing Default pose");
    }

    std::unordered_set<object_id> ids;
    for (const auto& p : poses) {
        add_asset_id(ids, p.id);
        for (const auto& [id, pt] : p.node_positions) {
            if (id.is_nil() || !std::isfinite(pt.x) || !std::isfinite(pt.y)) {
                throw std::invalid_argument("Invalid pose position");
            }
        }
    }

    for (const auto& a : animations) {
        validate_animation(a, ids);
    }
}

void sm::animation_assets::validate(const topology& topology,
        std::span<const object_id> rig_skeletons) const {
    validate();

    const std::unordered_set<object_id> rig(rig_skeletons.begin(), rig_skeletons.end());
    for (const auto id : rig_skeletons) {
        if (!topology.skeleton(id)) {
            throw std::invalid_argument("Character rig contains a missing skeleton");
        }
    }

    for (const auto& p : poses) {
        for (const auto& [id, pt] : p.node_positions) {
            auto node = topology.get<sm::node>(id);
            if (!node || !rig.contains(node->get().owner().id())) {
                throw std::invalid_argument("Pose contains a node outside the character rig");
            }
        }
    }

    for (const auto& a : animations) {
        for (const auto& keyframe : a.keyframes) {
            if (!skeletal_pose_compatible(keyframe.pose, topology, rig_skeletons)) {
                throw std::invalid_argument("Animation keyframe does not match the character rig");
            }
        }
        for (std::size_t i = 0; i < a.transitions.size(); ++i) {
            const auto& transition = a.transitions[i];
            for (const auto& [id, path] : transition.paths) {
                auto node = topology.get<sm::node>(id);
                if (!node || !rig.contains(node->get().owner().id()))
                    throw std::invalid_argument("Animation path node is outside character rig");
            }
            for (const auto id : transition.pinned_nodes) {
                auto node = topology.get<sm::node>(id);
                if (!node || !rig.contains(node->get().owner().id())) {
                    throw std::invalid_argument("Animation transition pin is outside the character rig");
                }
            }
            sm::constraint_map effective = topology.constraints();
            for (const auto& [cid, c] : transition.rotation_constraints) {
                const auto* rotation = c.rotation();
                if (!rotation)
                    throw std::invalid_argument("Animation transition contains a non-rotation constraint");
                auto bone = topology.get<sm::bone>(rotation->target_bone);
                if (!bone || !rig.contains(bone->get().owner().id()))
                    throw std::invalid_argument("Animation transition rotation constraint is outside the character rig");
                if (rotation->reference.kind == sm::rotation_reference_kind::bone) {
                    auto reference = topology.get<sm::bone>(rotation->reference.bone_id);
                    if (!reference || !rig.contains(reference->get().owner().id()))
                        throw std::invalid_argument("Animation transition rotation reference is outside the character rig");
                }
                if (!effective.emplace(cid, c).second)
                    throw std::invalid_argument("Animation transition constraint ID collides with a persistent constraint");
            }
            if (sm::validate_constraints(topology, effective) != sm::result::success)
                throw std::invalid_argument("Invalid animation transition rotation constraint");

            if (transition.pinned_nodes.empty())
                continue;

            sm::topology from_topology, to_topology;
            for (auto sid : rig_skeletons) {
                auto source = topology.skeleton(sid);
                if (!source || !source->get().copy_to(from_topology) || !source->get().copy_to(to_topology))
                    throw std::invalid_argument("Animation transition rig copy failed");
            }
            sm::apply_skeletal_pose(a.keyframes[i].pose, from_topology, rig_skeletons);
            sm::apply_skeletal_pose(a.keyframes[i + 1].pose, to_topology, rig_skeletons);
            for (const auto id : transition.pinned_nodes) {
                auto from = from_topology.get<sm::node>(id);
                auto to = to_topology.get<sm::node>(id);
                if (!from || !to)
                    throw std::invalid_argument("Animation transition pin is outside the character rig");
                if (sm::distance(from->get().world_pos(), to->get().world_pos()) > 1e-8)
                    throw std::invalid_argument("Animation transition pin endpoints do not match");
            }
        }
    }
}

sm::pose sm::capture_pose(const topology& topology, const std::vector<object_id>& skeletons,
        std::string name) {
    pose p;
    p.name = std::move(name);
    for (const auto& id : skeletons) {
        if (auto s = topology.skeleton(id)) {
            for (auto n : s->get().nodes()) {
                p.node_positions.emplace(n->id(), n->world_pos());
            }
        }
    }
    return p;
}

sm::skeletal_pose sm::capture_skeletal_pose(const topology& topology,
        std::span<const object_id> skeletons) {
    skeletal_pose result;
    for (const auto id : skeletons) {
        if (auto s = topology.skeleton(id)) {
            result.root_positions.emplace(
                s->get().root_node().id(), s->get().root_node().world_pos());
            for (auto bone : s->get().bones()) {
                result.bone_rotations.emplace(bone->id(), bone->rotation());
            }
        }
    }
    return result;
}

bool sm::skeletal_pose_compatible(const skeletal_pose& pose, const topology& topology,
        std::span<const object_id> skeletons) {
    std::size_t roots = 0;
    std::size_t bones = 0;

    for (const auto id : skeletons) {
        auto s = topology.skeleton(id);
        if (!s)
            return false;

        ++roots;
        if (!pose.root_positions.contains(s->get().root_node().id()))
            return false;

        for (auto bone : s->get().bones()) {
            ++bones;
            if (!pose.bone_rotations.contains(bone->id()))
                return false;
        }
    }

    return pose.root_positions.size() == roots && pose.bone_rotations.size() == bones;
}

void sm::apply_skeletal_pose(const skeletal_pose& pose, const topology& topology,
        std::span<const object_id> skeletons) {
    if (!skeletal_pose_compatible(pose, topology, skeletons)) {
        throw std::invalid_argument("Keyframe pose does not match rig");
    }

    // Snapshot the current bone lengths before moving any nodes. Computing a
    // child's length after its parent has already moved measures against the
    // child's old position and progressively distorts the rig.
    std::unordered_map<object_id, double> bone_lengths;
    for (const auto id : skeletons) {
        auto skel = topology.skeleton(id);
        if (!skel) {
            throw std::invalid_argument("Keyframe skeleton is missing from rig");
        }
        for (auto bone : skel->get().bones()) {
            bone_lengths.emplace(bone->id(), bone->scaled_length());
        }
    }

    geometry_batch batch(topology);
    std::function<void(sm::node&, double)> place_children =
        [&](sm::node& parent, double parent_world) {
            for (auto bone_ref : parent.child_bones()) {
                auto& bone = bone_ref.get();
                const double world = parent_world + pose.bone_rotations.at(bone.id());
                const double len = bone_lengths.at(bone.id());
                auto& child = bone.child_node();
                child.set_world_pos(parent.world_pos() +
                    point{len * std::cos(world), len * std::sin(world)});
                place_children(child, world);
            }
        };

    for (const auto id : skeletons) {
        auto skel = topology.skeleton(id);
        auto root_ref = topology.get<sm::node>(skel->get().root_node().id());
        if (!root_ref) {
            throw std::invalid_argument("Keyframe root is missing from rig");
        }

        auto& root = root_ref->get();
        root.set_world_pos(pose.root_positions.at(root.id()));
        place_children(root, 0.0);
    }

    if (batch.commit() != result::success) {
        throw std::invalid_argument("keyframe pose violates rigid constraints");
    }
}

void sm::initialize_animation_assets(animation_assets& assets, const topology& topology,
        const std::vector<object_id>& skeletons) {
    if (!assets.poses.empty() || skeletons.empty())
        return;

    auto p = capture_pose(topology, skeletons, "Default");
    assets.default_pose = p.id;
    assets.poses.push_back(std::move(p));
}

void sm::reconcile_animation_poses(animation_assets& assets, const topology& topology,
        const std::vector<object_id>& skeletons) {
    initialize_animation_assets(assets, topology, skeletons);
    if (assets.poses.empty())
        return;

    std::unordered_map<object_id, point> members;
    for (const auto& sid : skeletons) {
        if (auto skel = topology.skeleton(sid)) {
            for (auto node : skel->get().nodes()) {
                members.emplace(node->id(), node->world_pos());
            }
        }
    }

    // Structural edits can remove nodes from a character. Drop transition-local
    // paths to those nodes as part of the same reconciliation rather than leave
    // dangling object IDs that would invalidate the project on next save.
    for (auto& a : assets.animations)
        for (auto& tr : a.transitions)
            std::erase_if(tr.paths,[&](const auto& entry) {return !members.contains(entry.first);});

    for (auto& p : assets.poses) {
        if (p.id == assets.default_pose) {
            std::erase_if(p.node_positions,
                [&](const auto& entry) { return !members.contains(entry.first); });
            for (const auto& [id, pt] : members) {
                p.node_positions.try_emplace(id, pt);
            }
        } else {
            std::erase_if(p.node_positions,
                [&](const auto& entry) { return !members.contains(entry.first); });
        }
    }
}

void sm::remap_animation_assets(animation_assets& assets,
        const std::unordered_map<object_id, object_id>& id_remap) {
    auto remap_id = [&](object_id id) {
        if (auto it = id_remap.find(id); it != id_remap.end()) {
            return it->second;
        }
        return id;
    };

    for (auto& p : assets.poses) {
        std::unordered_map<object_id, point> remapped;
        for (const auto& [old_id, pt] : p.node_positions) {
            if (!remapped.emplace(remap_id(old_id), pt).second) {
                throw std::invalid_argument("Pose remap produced duplicate node IDs");
            }
        }
        p.node_positions = std::move(remapped);
    }

    for (auto& a : assets.animations) {
        for (auto& keyframe : a.keyframes) {
            std::unordered_map<object_id, point> roots;
            for (const auto& [old_id, pt] : keyframe.pose.root_positions) {
                if (!roots.emplace(remap_id(old_id), pt).second) {
                    throw std::invalid_argument("Keyframe remap produced duplicate root IDs");
                }
            }
            keyframe.pose.root_positions = std::move(roots);

            std::unordered_map<object_id, double> bones;
            for (const auto& [old_id, angle] : keyframe.pose.bone_rotations) {
                if (!bones.emplace(remap_id(old_id), angle).second) {
                    throw std::invalid_argument("Keyframe remap produced duplicate bone IDs");
                }
            }
            keyframe.pose.bone_rotations = std::move(bones);

        }
        for (auto& transition : a.transitions) {
            std::unordered_set<object_id> pins;
            for (const auto old_id : transition.pinned_nodes) {
                if (!pins.insert(remap_id(old_id)).second) {
                    throw std::invalid_argument("Transition remap produced duplicate pin IDs");
                }
            }
            transition.pinned_nodes = std::move(pins);
            constraint_map constraints;
            for (const auto& [cid, c] : transition.rotation_constraints) {
                auto remapped = c.remapped(id_remap);
                if (!constraints.emplace(remapped.id(), std::move(remapped)).second)
                    throw std::invalid_argument("Transition remap produced duplicate constraint IDs");
            }
            transition.rotation_constraints = std::move(constraints);
            std::map<object_id, animation_path> remapped_paths;
            for (auto& [old_id, path] : transition.paths) {
                path.node = remap_id(old_id);
                path.invalidate();
                if (!remapped_paths.emplace(path.node, std::move(path)).second)
                    throw std::invalid_argument("Transition remap produced duplicate path node IDs");
            }
            transition.paths = std::move(remapped_paths);
        }
    }
}

void sm::apply_pose(const pose& pose, const topology& topology) {
    geometry_batch batch(topology);
    for (const auto& [id, pt] : pose.node_positions) {
        if (auto node = topology.get<sm::node>(id)) {
            node->get().set_world_pos(pt);
        }
    }
    if (batch.commit() != result::success) {
        throw std::invalid_argument("pose violates rigid constraints");
    }
}

bool sm::pose_compatible(const pose& pose, const topology& topology,
        const std::vector<object_id>& skeletons) {
    std::size_t count = 0;
    for (const auto& id : skeletons) {
        if (auto s = topology.skeleton(id)) {
            for (auto n : s->get().nodes()) {
                ++count;
                if (!pose.node_positions.contains(n->id()))
                    return false;
            }
        }
    }
    return count == pose.node_positions.size();
}

std::optional<sm::point> sm::animation_pose_node(const skeletal_pose& pose,
    const topology& source, std::span<const object_id> rig, object_id id) {
    try {
        topology copy;
        for (auto sid : rig) {
            auto skel=source.skeleton(sid);
            if (!skel || !skel->get().copy_to(copy)) return {};
        }
        apply_skeletal_pose(pose,copy,rig);
        if (auto n=copy.get<node>(id)) return n->get().world_pos();
    } catch (...) {}
    return {};
}
std::optional<sm::animation_root_frame> sm::fixed_animation_root(const animation& a,
    const topology& source, std::span<const object_id> rig,
    std::optional<object_id> root_bone) {
    if (a.keyframes.empty()) return {};
    try {
        topology copy;
        for (auto sid : rig) {
            auto skel=source.skeleton(sid);
            if (!skel || !skel->get().copy_to(copy)) return {};
        }
        apply_skeletal_pose(a.keyframes.front().pose,copy,rig);
        if (!root_bone) {
            for (auto sid : rig) {
                auto skel=copy.skeleton(sid);
                if (!skel) continue;
                for (auto bone : skel->get().bones()) {
                    root_bone=bone->id(); break;
                }
                if (root_bone) break;
            }
        }
        if (!root_bone) return {};
        auto b=copy.get<bone>(*root_bone);
        if (!b) return {};
        return animation_root_frame{b->get().parent_node().world_pos(),b->get().world_rotation()};
    } catch (...) { return {}; }
}
