#include "sm_animation.hpp"
#include "sm_geometry_batch.hpp"
#include "sm_skeleton.hpp"
#include <algorithm>
#include <cmath>
#include <functional>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

const sm::pose* sm::animation_assets::find_pose(object_id id) const {
    for (const auto& p : poses) {
        if (p.id == id) return &p;
    }
    return nullptr;
}

const sm::animation* sm::animation_assets::find_animation(object_id id) const {
    for (const auto& a : animations) {
        if (a.id == id) return &a;
    }
    return nullptr;
}

sm::animation* sm::animation_assets::find_animation(object_id id) {
    for (auto& a : animations) {
        if (a.id == id) return &a;
    }
    return nullptr;
}

const sm::pose_keyframe* sm::animation::find_keyframe(object_id id) const {
    for (const auto& keyframe : keyframes) {
        if (keyframe.id == id) return &keyframe;
    }
    return nullptr;
}

sm::pose_keyframe* sm::animation::find_keyframe(object_id id) {
    for (auto& keyframe : keyframes) {
        if (keyframe.id == id) return &keyframe;
    }
    return nullptr;
}

std::optional<std::size_t> sm::animation::keyframe_index(object_id id) const {
    for (std::size_t i = 0; i < keyframes.size(); ++i) {
        if (keyframes[i].id == id) return i;
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

void sm::animation_assets::validate() const {
    if (!find_pose(default_pose)) {
        throw std::invalid_argument("Missing Default pose");
    }

    std::unordered_set<object_id> ids;
    auto add = [&](object_id id) {
        if (id.is_nil() || !ids.insert(id).second) {
            throw std::invalid_argument("Duplicate animation asset ID");
        }
    };

    for (const auto& p : poses) {
        add(p.id);
        for (const auto& [id, pt] : p.node_positions) {
            if (id.is_nil() || !std::isfinite(pt.x) || !std::isfinite(pt.y)) {
                throw std::invalid_argument("Invalid pose position");
            }
        }
    }

    for (const auto& a : animations) {
        add(a.id);
        const auto wanted = a.keyframes.empty() ? std::size_t{} : a.keyframes.size() - 1;
        if (a.transitions.size() != wanted) {
            throw std::invalid_argument("Animation transition count does not match keyframes");
        }

        for (const auto& keyframe : a.keyframes) {
            add(keyframe.id);
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
            add(transition.id);
            if (!(transition.duration_seconds > 0.0) ||
                    !std::isfinite(transition.duration_seconds)) {
                throw std::invalid_argument("Invalid transition duration");
            }
        }
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
        if (!s) return false;

        ++roots;
        if (!pose.root_positions.contains(s->get().root_node().id())) return false;

        for (auto bone : s->get().bones()) {
            ++bones;
            if (!pose.bone_rotations.contains(bone->id())) return false;
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
    if (!assets.poses.empty() || skeletons.empty()) return;

    auto p = capture_pose(topology, skeletons, "Default");
    assets.default_pose = p.id;
    assets.poses.push_back(std::move(p));
}

void sm::reconcile_animation_poses(animation_assets& assets, const topology& topology,
        const std::vector<object_id>& skeletons) {
    initialize_animation_assets(assets, topology, skeletons);
    if (assets.poses.empty()) return;

    std::unordered_map<object_id, point> members;
    for (const auto& sid : skeletons) {
        if (auto skel = topology.skeleton(sid)) {
            for (auto node : skel->get().nodes()) {
                members.emplace(node->id(), node->world_pos());
            }
        }
    }

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
                if (!pose.node_positions.contains(n->id())) return false;
            }
        }
    }
    return count == pose.node_positions.size();
}
