#include "sm_project.hpp"
#include "sm_artwork_io.hpp"
#include <set>
#include "json.hpp"
#include "miniz.h"
#include <cstring>
#include <stdexcept>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <algorithm>
#include <cassert>
#include <charconv>
#include <limits>

using json = nlohmann::json;

/*------------------------------------------------------------------------------------------------*/

namespace {
    template<class... Ts> struct overloaded : Ts... { using Ts::operator()...; };

    constexpr std::string_view project_json_name = "project.json";
    constexpr double project_json_version = 6.0;

    sm::object_id default_character_root_bone(
            const sm::topology& topology,const std::vector<sm::object_id>& skeletons) {
        if(skeletons.empty()) return {};
        auto skeleton=topology.skeleton(skeletons.front());
        if(!skeleton) return {};
        const auto root_bones=skeleton->get().root_node().child_bones();
        return root_bones.empty() ? sm::object_id{} : root_bones.front()->id();
    }

    template<typename Object>
    sm::object_id object_id_of(const Object& object) {
        return std::visit(
            [](auto ref) { return ref->id(); },
            object
        );
    }

    template<typename Object, typename CharacterTable>
    bool build_object_index(
            sm::topology& topology,
            CharacterTable& characters,
            std::unordered_map<sm::object_id, Object>& objects) {
        objects.clear();
        auto insert = [&objects](Object object) {
            return objects.emplace(object_id_of(object), object).second;
        };

        for (auto skel : topology.skeletons()) {
            if (!insert(skel)) {
                objects.clear();
                return false;
            }
            for (auto node : skel->nodes()) {
                if (!insert(node)) {
                    objects.clear();
                    return false;
                }
            }
            for (auto bone : skel->bones()) {
                if (!insert(bone)) {
                    objects.clear();
                    return false;
                }
            }
        }
        for (auto& entry : characters) {
            if (!insert(sm::character_ref(*entry.second))) {
                objects.clear();
                return false;
            }
        }
        return true;
    }
}

/*------------------------------------------------------------------------------------------------*/

void sm::project::invalidate_object_index() noexcept {
    object_index_dirty_ = true;
}

bool sm::project::rebuild_object_index() {
    if (!build_object_index(topology_, characters_, objects_)) {
        return false;
    }
    object_index_dirty_ = false;
    return true;
}

bool sm::project::ensure_object_index() const {
    if (!object_index_dirty_) {
        return true;
    }
    return const_cast<project*>(this)->rebuild_object_index();
}

const sm::topology& sm::project::topology() const {
    return topology_;
}

sm::skeleton& sm::project::create_skeleton(const point& pt) {
    // topology generates skeleton/root-node IDs without knowledge of character IDs.
    // Retry the vanishingly unlikely collision so the project-wide namespace remains strict.
    while (true) {
        auto& created = topology_.create_skeleton(pt);
        const auto created_id = created.id();
        invalidate_object_index();
        if (ensure_object_index()) {
            return created;
        }
        topology_.delete_skeleton(created_id);
        invalidate_object_index();
    }
}

sm::expected_skel sm::project::copy_skeleton(
        const skeleton& source,
        const std::unordered_map<object_id, object_id>& id_remap) {
    if (!ensure_object_index()) {
        return std::unexpected(result::duplicate_id);
    }

    auto mapped_id = [&id_remap](const object_id& id) {
        auto it = id_remap.find(id);
        return it == id_remap.end() ? id : it->second;
    };
    auto collides = [this, &mapped_id](const object_id& id) {
        return objects_.contains(mapped_id(id));
    };

    if (collides(source.id())) {
        return std::unexpected(result::duplicate_id);
    }
    for (auto node : source.nodes()) {
        if (collides(node->id())) {
            return std::unexpected(result::duplicate_id);
        }
    }
    for (auto bone : source.bones()) {
        if (collides(bone->id())) {
            return std::unexpected(result::duplicate_id);
        }
    }

    auto copied = source.copy_to(topology_, id_remap);
    if (!copied) {
        return copied;
    }
    invalidate_object_index();
    if (!ensure_object_index()) {
        throw std::runtime_error("copying skeleton produced duplicate object IDs");
    }
    return copied;
}

sm::result sm::project::delete_skeleton(const object_id& id) {
    auto skel = topology_.skeleton(id);
    if (!skel) return skel.error();
    std::vector<object_id> removed_nodes;
    std::vector<object_id> removed_bones;
    for (auto node : skel->get().nodes()) removed_nodes.push_back(node->id());
    for (auto bone : skel->get().bones()) removed_bones.push_back(bone->id());
    const auto effects = effects_for_removed_objects(
        std::move(removed_nodes), std::move(removed_bones), {id});
    detach_skeleton(skel->get());
    auto deleted = topology_.delete_skeleton(id);
    if (deleted != result::success) {
        return deleted;
    }
    prune_empty_characters();
    for (auto& [id, c] : characters_) {
        repair_character_root_bone(*c);
        initialize_animation_assets(c->animation_data_, topology_, c->rig().skeleton_ids());
    }
    erase_cascade_actions(effects);
    invalidate_object_index();
    if (!ensure_object_index()) {
        throw std::runtime_error("deleting skeleton left duplicate object IDs");
    }
    assert(has_consistent_membership());
    assert_animation_references_resolve();
    return result::success;
}

void sm::project::detach_skeleton(skeleton& skel) {
    if (auto parent = skel.parent_character()) {
        auto& ids = characters_.at(parent->get().id())->rig_.skeleton_ids_;
        std::erase(ids, skel.id());
        skel.clear_parent_character();
    }
}

void sm::project::prune_empty_characters() {
    std::erase_if(characters_, [](const auto& entry) { return entry.second->rig().empty(); });
    invalidate_object_index();
}

void sm::project::repair_character_root_bone(sm::character& character) {
    if(!character.character_root_bone().is_nil()) {
        if(auto bone=topology_.get<sm::bone>(character.character_root_bone())) {
            auto parent=bone->get().owner().parent_character();
            if(parent && parent->get().id()==character.id()) return;
        }
    }
    character.set_character_root_bone(default_character_root_bone(topology_,character.rig().skeleton_ids()));
}

sm::topology_edit_effects sm::project::effects_for_removed_objects(
        std::vector<object_id> nodes,
        std::vector<object_id> bones,
        std::vector<object_id> skeletons) const {
    auto normalize = [](auto& ids) {
        std::ranges::sort(ids);
        ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    };
    normalize(nodes);
    normalize(bones);
    normalize(skeletons);

    topology_edit_effects effects{std::move(nodes), std::move(bones), std::move(skeletons), {}};
    const std::unordered_set<object_id> removed_nodes(effects.removed_nodes.begin(), effects.removed_nodes.end());
    const std::unordered_set<object_id> removed_bones(effects.removed_bones.begin(), effects.removed_bones.end());
    const std::unordered_set<object_id> removed_skeletons(effects.removed_skeletons.begin(), effects.removed_skeletons.end());

    auto removed = [&](const animation_dependency& dependency) {
        switch (dependency.kind) {
        case animation_dependency_kind::node: return removed_nodes.contains(dependency.id);
        case animation_dependency_kind::bone: return removed_bones.contains(dependency.id);
        case animation_dependency_kind::skeleton: return removed_skeletons.contains(dependency.id);
        }
        return false;
    };

    for (const auto& [character_id, character] : characters_) {
        for (const auto& animation : character->animation_data().animations) {
            for (const auto& layer : animation.layers) for (const auto& action : layer.actions) {
                const auto dependencies = animation_action_dependencies(action);
                if (std::ranges::any_of(dependencies, removed))
                    effects.removed_animation_actions.push_back({character_id, animation.id, action.id});
            }
        }
    }
    std::ranges::sort(effects.removed_animation_actions, [](const auto& a, const auto& b) {
        if (a.character != b.character) return a.character < b.character;
        if (a.animation != b.animation) return a.animation < b.animation;
        return a.action < b.action;
    });
    return effects;
}

void sm::project::erase_cascade_actions(animation_assets& assets, const topology_edit_effects& effects) {
    const std::unordered_set<object_id> removed_nodes(effects.removed_nodes.begin(), effects.removed_nodes.end());
    const std::unordered_set<object_id> removed_bones(effects.removed_bones.begin(), effects.removed_bones.end());
    const std::unordered_set<object_id> removed_skeletons(effects.removed_skeletons.begin(), effects.removed_skeletons.end());
    auto invalid = [&](const animation_action& action) {
        for (const auto& dependency : animation_action_dependencies(action)) {
            switch (dependency.kind) {
            case animation_dependency_kind::node:
                if (removed_nodes.contains(dependency.id)) return true;
                break;
            case animation_dependency_kind::bone:
                if (removed_bones.contains(dependency.id)) return true;
                break;
            case animation_dependency_kind::skeleton:
                if (removed_skeletons.contains(dependency.id)) return true;
                break;
            }
        }
        return false;
    };
    for (auto& animation : assets.animations)
        for (auto& layer : animation.layers)
            std::erase_if(layer.actions, invalid);
}

void sm::project::erase_cascade_actions(const topology_edit_effects& effects) {
    if (!effects.has_animation_cascade()) return;
    for (auto& [id, character] : characters_)
        erase_cascade_actions(character->animation_data_, effects);
}

bool sm::project::has_valid_animation_references() const {
    for (const auto& [id, character] : characters_) {
        for (const auto& animation : character->animation_data().animations) {
            for (const auto& layer : animation.layers) for (const auto& action : layer.actions) {
                for (const auto& dependency : animation_action_dependencies(action)) {
                    switch (dependency.kind) {
                    case animation_dependency_kind::node:
                        if (!topology_.get<sm::node>(dependency.id)) return false;
                        break;
                    case animation_dependency_kind::bone:
                        if (!topology_.get<sm::bone>(dependency.id)) return false;
                        break;
                    case animation_dependency_kind::skeleton:
                        if (!topology_.skeleton(dependency.id)) return false;
                        break;
                    }
                }
            }
        }
    }
    return true;
}

void sm::project::assert_animation_references_resolve() const {
    assert(has_valid_animation_references());
}

sm::result sm::project::can_create_bone(const node& u, const node& v) const {
    if (&u.owner().owner() != &topology_ || &v.owner().owner() != &topology_)
        return result::foreign_skeleton;
    const auto a = u.owner().parent_character(), b = v.owner().parent_character();
    if (a && b && a->get().id() != b->get().id()) return result::different_characters;
    if (&u.owner() == &v.owner()) return result::cyclic_bones;
    if (!v.is_root()) return result::multi_parent_node;
    return result::success;
}

std::expected<sm::topology_edit_effects, sm::result> sm::project::preview_create_bone(
        const node& u, const node& v) const {
    if (const auto status = can_create_bone(u, v); status != result::success)
        return std::unexpected(status);
    return effects_for_removed_objects({}, {}, {v.owner().id()});
}

sm::expected_bone sm::project::create_bone(const std::string& name, node& u, node& v) {
    if (!ensure_object_index()) {
        return std::unexpected(result::duplicate_id);
    }
    object_id id;
    do {
        id = object_id::generate();
    } while (objects_.contains(id));
    return create_bone(id, name, u, v);
}

sm::expected_bone sm::project::create_bone(
        object_id id, const std::string& name, node& u, node& v) {
    if (auto status = can_create_bone(u, v); status != result::success)
        return std::unexpected(status);
    if (!ensure_object_index()) {
        return std::unexpected(result::duplicate_id);
    }
    if (objects_.contains(id)) {
        return std::unexpected(result::duplicate_id);
    }
    const auto effects = effects_for_removed_objects({}, {}, {v.owner().id()});
    const auto removed_id = v.owner().id();
    const auto parent_u = u.owner().parent_character(), parent_v = v.owner().parent_character();
    auto parent = parent_u ? parent_u : parent_v;
    auto created = topology_.create_bone(id, name, u, v);
    if (!created) {
        return created;
    }
    if (parent) {
        auto& character = *characters_.at(parent->get().id());
        std::erase(character.rig_.skeleton_ids_, removed_id);
        auto& merged = created->get().owner();
        if (!character.rig_.contains(merged.id())) character.rig_.add_skeleton(merged.id());
        merged.set_parent_character(character);
        repair_character_root_bone(character);
    }
    invalidate_object_index();
    if (!ensure_object_index()) {
        throw std::runtime_error("creating bone produced duplicate object IDs");
    }
    erase_cascade_actions(effects);
    assert(has_consistent_membership());
    assert_animation_references_resolve();
    return created;
}

sm::topology_change sm::project::replace_skeletons(
        const std::vector<object_id>& replacees,
        const std::vector<skel_ref>& replacements,
        const std::unordered_set<object_id>& regenerate_ids,
        const membership_state* restored_membership) {
    topology_change change;
    auto plan = plan_replacement(replacees, replacements, regenerate_ids, restored_membership);
    if (!plan) { change.status = plan.error(); return change; }
    change.effects = plan->effects;
    change.removed_skeleton_ids.reserve(replacees.size());
    change.added_skeleton_ids.reserve(replacements.size());

    if (!ensure_object_index()) {
        throw std::runtime_error("project contains duplicate object IDs");
    }
    std::unordered_set<object_id> released_ids;
    for (const auto& id : replacees) {
        auto skel = topology_.skeleton(id);
        released_ids.insert(id);
        for (auto node : skel->get().nodes()) released_ids.insert(node->id());
        for (auto bone : skel->get().bones()) released_ids.insert(bone->id());
    }
    std::unordered_set<object_id> used_ids;
    used_ids.reserve(objects_.size());
    for (const auto& [id, object] : objects_) {
        if (!released_ids.contains(id)) used_ids.insert(id);
    }
    for (const auto& c : plan->membership.characters) used_ids.insert(c.id);

    auto allocation_guard = used_ids;
    sm::topology staged;
    std::unordered_map<object_id, std::optional<object_id>> staged_parents;
    for (auto replacement : replacements) {
        allocation_guard.insert(replacement->id());
        for (auto node : replacement->nodes()) {
            allocation_guard.insert(node->id());
        }
        for (auto bone : replacement->bones()) {
            allocation_guard.insert(bone->id());
        }
    }
    auto unused_object_id = [&allocation_guard]() {
        while (true) {
            auto id = object_id::generate();
            if (allocation_guard.insert(id).second) {
                return id;
            }
        }
    };

    for (auto replacement : replacements) {
        std::unordered_map<object_id, object_id> id_remap;
        auto reserve_id = [&](const object_id& id) {
            if (regenerate_ids.contains(id) || used_ids.contains(id)) {
                auto new_id = unused_object_id();
                used_ids.insert(new_id);
                id_remap[id] = new_id;
            } else {
                used_ids.insert(id);
            }
        };

        reserve_id(replacement->id());
        for (auto node : replacement->nodes()) {
            reserve_id(node->id());
        }
        for (auto bone : replacement->bones()) {
            reserve_id(bone->id());
        }

        auto copied = replacement->copy_to(staged, id_remap);
        if (!copied) {
            topology_change failed;
            failed.status = copied.error();
            return failed;
        }
        // Artwork follows surviving bones when replacement assigns fresh IDs.
        // Animation action references are deliberately not remapped here: the replacement
        // plan has already removed any action whose persistent dependency is losing identity.
        const auto parent = plan->membership.parents.at(replacement->id());
        if (parent) {
            std::unordered_map<object_id, object_id> bone_remap;
            for (auto bone : replacement->bones())
                if (auto it = id_remap.find(bone->id()); it != id_remap.end()) bone_remap.emplace(*it);
            for (auto& state : plan->membership.characters) if (state.id == *parent) {
                state.artwork.remap_bones(bone_remap);
                if(auto it=bone_remap.find(state.character_root_bone);it!=bone_remap.end())
                    state.character_root_bone=it->second;
            }
        }
        change.added_skeleton_ids.push_back(copied->get().id());
        staged_parents.emplace(copied->get().id(), plan->membership.parents.at(replacement->id()));
    }
    // All validation, ID allocation and topology copying precede live erasure.
    // Keep character objects alive until their replacement components are attached.
    for (const auto& id : replacees) {
        detach_skeleton(topology_.skeleton(id)->get());
        topology_.delete_skeleton(id);
        change.removed_skeleton_ids.push_back(id);
    }
    for (const auto& state : plan->membership.characters) {
        if (!characters_.contains(state.id)) characters_.emplace(state.id,
            sm::character::make_unique(*this, state.id, state.name, sm::rig(*this), state.character_root_bone));
        characters_.at(state.id)->character_root_bone_ = state.character_root_bone;
        characters_.at(state.id)->artwork_ = state.artwork;
        characters_.at(state.id)->animation_data_ = state.animation_data;
    }
    invalidate_object_index();
    for (const auto& id : change.added_skeleton_ids) {
        auto copied = copy_skeleton(staged.skeleton(id)->get());
        if (!copied) throw std::runtime_error("validated skeleton restoration failed");
        if (auto parent = staged_parents.at(id)) {
            auto& character = *characters_.at(*parent);
            character.rig_.add_skeleton(copied->get().id());
            copied->get().set_parent_character(character);
        }
    }
    prune_empty_characters();
    for (auto& [id, c] : characters_) {
        repair_character_root_bone(*c);
        initialize_animation_assets(c->animation_data_, topology_, c->rig().skeleton_ids());
    }
    assert(has_consistent_membership());
    assert_animation_references_resolve();
    return change;
}

sm::membership_state sm::project::snapshot_membership(
        const std::vector<object_id>& ids, std::span<const object_id> extra_characters) const {
    membership_state state;
    std::unordered_set<object_id> seen;
    for (const auto& id : ids) {
        auto skel = topology_.skeleton(id);
        if (!skel) continue;
        auto parent = skel->get().parent_character();
        state.parents[id] = parent ? std::optional(parent->get().id()) : std::nullopt;
        if (parent && seen.insert(parent->get().id()).second)
            state.characters.push_back({parent->get().id(), parent->get().name(), parent->get().character_root_bone(),
                parent->get().artwork(), parent->get().animation_data()});
    }
    for (const auto& id : extra_characters) {
        auto it = characters_.find(id);
        if (it != characters_.end() && seen.insert(id).second)
            state.characters.push_back({it->second->id(), it->second->name(), it->second->character_root_bone(),
                it->second->artwork(), it->second->animation_data()});
    }
    return state;
}

sm::result sm::project::restore_membership(const membership_state& state) {
    if (!ensure_object_index()) return result::duplicate_id;
    character_tbl prepared;
    std::unordered_set<object_id> metadata;
    for (const auto& c : state.characters) {
        if (!metadata.insert(c.id).second) return result::invalid_membership;
        if (!characters_.contains(c.id)) {
            if (objects_.contains(c.id)) return result::duplicate_id;
            prepared.emplace(c.id, sm::character::make_unique(*this, c.id, c.name, sm::rig(*this), c.character_root_bone));
            prepared.at(c.id)->artwork_ = c.artwork;
            prepared.at(c.id)->animation_data_ = c.animation_data;
        }
    }
    for (const auto& [sid, parent] : state.parents) {
        if (!topology_.skeleton(sid)) return result::not_found;
        if (parent && !metadata.contains(*parent)) return result::invalid_membership;
    }
    for (const auto& [sid, parent] : state.parents) detach_skeleton(topology_.skeleton(sid)->get());
    characters_.merge(prepared);
    for (const auto& [sid, parent] : state.parents) {
        if (!parent) continue;
        auto& c = *characters_.at(*parent);
        c.rig_.add_skeleton(sid);
        topology_.skeleton(sid)->get().set_parent_character(c);
    }
    prune_empty_characters();
    for (auto& [id, c] : characters_) {
        repair_character_root_bone(*c);
        initialize_animation_assets(c->animation_data_, topology_, c->rig().skeleton_ids());
    }
    assert(has_consistent_membership());
    return result::success;
}

std::expected<sm::replacement_plan, sm::result> sm::project::plan_replacement(
        const std::vector<object_id>& replacees, const std::vector<skel_ref>& replacements,
        const std::unordered_set<object_id>& regenerate_ids,
        const membership_state* restored) const {
    replacement_plan plan;
    std::unordered_set<object_id> removed, affected_characters;
    std::unordered_set<object_id> old_nodes, old_bones;
    std::unordered_map<object_id, std::optional<object_id>> node_parents;
    for (const auto& id : replacees) {
        if (!removed.insert(id).second) return std::unexpected(result::duplicate_skeleton);
        auto skel = topology_.skeleton(id);
        if (!skel) return std::unexpected(result::not_found);
        auto parent = skel->get().parent_character();
        std::optional<object_id> cid = parent ? std::optional(parent->get().id()) : std::nullopt;
        if (cid) affected_characters.insert(*cid);
        for (auto node : skel->get().nodes()) {
            node_parents.emplace(node->id(), cid);
            old_nodes.insert(node->id());
        }
        for (auto bone : skel->get().bones()) old_bones.insert(bone->id());
    }
    if (restored) plan.membership.characters = restored->characters;
    else plan.membership.characters = snapshot_membership(replacees).characters;
    std::unordered_set<object_id> metadata;
    for (const auto& c : plan.membership.characters) {
        if (!metadata.insert(c.id).second) return std::unexpected(result::invalid_membership);
        // A restored character ID must not collide with any topology object, including
        // replacement IDs (which are remapped later while reserving character IDs).
        if (!characters_.contains(c.id)) {
            if (!ensure_object_index()) return std::unexpected(result::duplicate_id);
            if (objects_.contains(c.id)) return std::unexpected(result::duplicate_id);
        }
    }
    for (auto replacement : replacements) {
        // Sources may be live or scratch: replacement stages every copy before
        // erasing anything. Ownership comes from provenance, never source parents.
        if (replacement->empty()) return std::unexpected(result::invalid_membership);
        if (plan.membership.parents.contains(replacement->id()))
            return std::unexpected(result::duplicate_skeleton);
        bool has_source = false;
        std::optional<object_id> source_parent;
        for (auto node : replacement->nodes()) {
            auto it = node_parents.find(node->id());
            if (it == node_parents.end()) continue;
            has_source = true;
            if (!it->second) continue;
            if (source_parent && source_parent != it->second)
                return std::unexpected(result::different_characters);
            source_parent = it->second;
        }
        std::optional<object_id> parent;
        if (restored) {
            auto it = restored->parents.find(replacement->id());
            if (it == restored->parents.end()) return std::unexpected(result::invalid_membership);
            parent = it->second;
            if (parent && !metadata.contains(*parent)) return std::unexpected(result::invalid_membership);
            if (parent && source_parent && parent != source_parent)
                return std::unexpected(result::different_characters);
        } else {
            parent = source_parent;
            // No guessing from affected-character count or iteration order. Callers
            // replacing all node identities must supply explicit semantic state.
            if (!has_source && !affected_characters.empty())
                return std::unexpected(result::ambiguous_membership);
        }
        plan.membership.parents.emplace(replacement->id(), parent);
    }

    std::unordered_set<object_id> preserved_nodes, preserved_bones, preserved_skeletons;
    for (auto replacement : replacements) {
        if (!regenerate_ids.contains(replacement->id())) preserved_skeletons.insert(replacement->id());
        for (auto node : replacement->nodes())
            if (!regenerate_ids.contains(node->id())) preserved_nodes.insert(node->id());
        for (auto bone : replacement->bones())
            if (!regenerate_ids.contains(bone->id())) preserved_bones.insert(bone->id());
    }
    std::vector<object_id> removed_nodes, removed_bones, removed_skeletons;
    for (const auto& id : old_nodes) if (!preserved_nodes.contains(id)) removed_nodes.push_back(id);
    for (const auto& id : old_bones) if (!preserved_bones.contains(id)) removed_bones.push_back(id);
    for (const auto& id : removed) if (!preserved_skeletons.contains(id)) removed_skeletons.push_back(id);
    plan.effects = effects_for_removed_objects(
        std::move(removed_nodes), std::move(removed_bones), std::move(removed_skeletons));

    // A malformed cross-character action can still reference an object owned by a
    // different character. Preserve atomic undo for that case too by snapshotting
    // every character whose action is part of the semantic cascade.
    for (const auto& removed_action : plan.effects.removed_animation_actions) {
        if (metadata.insert(removed_action.character).second) {
            const auto& c = *characters_.at(removed_action.character);
            plan.membership.characters.push_back({c.id(), c.name(), c.character_root_bone(),
                c.artwork(), c.animation_data()});
        }
    }
    for (auto& state : plan.membership.characters)
        erase_cascade_actions(state.animation_data, plan.effects);

    for (const auto& id : affected_characters) {
        const auto& rig = characters_.at(id)->rig();
        bool survives = std::ranges::any_of(rig.skeleton_ids(), [&](const auto& sid) { return !removed.contains(sid); });
        for (const auto& [sid, parent] : plan.membership.parents) survives |= parent == id;
        if (!survives) plan.deleted_character_ids.push_back(id);
    }
    return plan;
}

std::expected<sm::topology_edit_effects, sm::result> sm::project::preview_replace_skeletons(
        const std::vector<object_id>& replacees,
        const std::vector<skel_ref>& replacements,
        const std::unordered_set<object_id>& regenerate_ids) const {
    auto plan = plan_replacement(replacees, replacements, regenerate_ids);
    if (!plan) return std::unexpected(plan.error());
    return plan->effects;
}

bool sm::project::has_consistent_membership() const {
    for (const auto& [id, c] : characters_) {
        if (c->rig().empty() || &c->owner() != this) return false;
        std::unordered_set<object_id> seen;
        for (const auto& sid : c->rig().skeleton_ids()) {
            auto s = topology_.skeleton(sid);
            if (!seen.insert(sid).second || !s || !s->get().parent_character() ||
                &s->get().parent_character()->get() != c.get()) return false;
        }
        if(!c->character_root_bone().is_nil()) {
            auto bone=topology_.get<sm::bone>(c->character_root_bone());
            if(!bone) return false;
            auto parent=bone->get().owner().parent_character();
            if(!parent || &parent->get()!=c.get()) return false;
        }
    }
    for (auto s : topology_.skeletons()) {
        if (auto p = s->parent_character()) {
            // Compare addresses before dereferencing the non-owning reference.
            auto it = std::ranges::find_if(characters_, [&](const auto& entry) { return entry.second.get() == &p->get(); });
            if (it == characters_.end() || !it->second->rig().contains(s->id())) return false;
        }
    }
    return true;
}

sm::result sm::project::adopt_skeletons(const object_id& id, std::span<const const_skel_ref> skeletons) {
    auto it = characters_.find(id);
    if (it == characters_.end()) return result::not_found;
    if (skeletons.empty()) return result::empty_character;
    std::unordered_set<object_id> seen;
    for (auto skel : skeletons) {
        if (&skel->owner() != &topology_) return result::foreign_skeleton;
        auto live = topology_.skeleton(skel->id());
        if (!live || &live->get() != &skel.get()) return result::foreign_skeleton;
        if (!seen.insert(skel->id()).second) return result::duplicate_skeleton;
        if (!skel->is_loose()) return result::skeleton_already_owned;
    }
    for (auto skel : skeletons) {
        auto live = topology_.skeleton(skel->id());
        it->second->rig_.add_skeleton(skel->id());
        live->get().set_parent_character(*it->second);
    }
    repair_character_root_bone(*it->second);
    assert(has_consistent_membership());
    return result::success;
}

sm::expected_const_character sm::project::create_character(std::span<const const_skel_ref> skeletons) {
    if (skeletons.empty()) {
        return std::unexpected(result::empty_character);
    }
    if (!ensure_object_index()) {
        return std::unexpected(result::duplicate_id);
    }

    std::unordered_set<object_id> seen;
    seen.reserve(skeletons.size());
    std::vector<skel_ref> validated;
    validated.reserve(skeletons.size());

    // Validate the complete request before changing either side of membership.
    for (auto skel : skeletons) {
        if (&skel->owner() != &topology_) {
            return std::unexpected(result::foreign_skeleton);
        }
        auto live = topology_.skeleton(skel->id());
        if (!live || &live->get() != &skel.get()) {
            return std::unexpected(result::foreign_skeleton);
        }
        if (!seen.insert(skel->id()).second) {
            return std::unexpected(result::duplicate_skeleton);
        }
        if (!live->get().is_loose()) {
            return std::unexpected(result::skeleton_already_owned);
        }
        validated.push_back(*live);
    }

    object_id id;
    do {
        id = object_id::generate();
    } while (objects_.contains(id));

    sm::rig rig(*this);
    for (auto skel : validated) {
        rig.add_skeleton(skel->id());
    }
    const auto root_bone=default_character_root_bone(topology_,rig.skeleton_ids());

    auto name = "character-" + std::to_string(next_character_name_);
    auto created = sm::character::make_unique(*this, id, std::move(name), std::move(rig), root_bone);
    auto* created_ptr = created.get();
    auto [it, inserted] = characters_.emplace(id, std::move(created));
    if (!inserted) {
        return std::unexpected(result::duplicate_id);
    }
    for (auto skel : validated) {
        skel->set_parent_character(*created_ptr);
    }
    initialize_animation_assets(created_ptr->animation_data_, topology_, created_ptr->rig().skeleton_ids());
    ++next_character_name_;

    invalidate_object_index();
    if (!ensure_object_index()) {
        throw std::runtime_error("creating character produced duplicate object IDs");
    }
    return const_character_ref(std::as_const(*created_ptr));
}

sm::result sm::project::remove_character(const object_id& id) {
    auto it = characters_.find(id);
    if (it == characters_.end()) {
        return result::not_found;
    }

    auto& removed = *it->second;
    // Scan live skeletons rather than trusting the rig alone. This guarantees no surviving
    // skeleton can retain the non-owning reference when the character is destroyed.
    for (auto skel : topology_.skeletons()) {
        auto parent = skel->parent_character();
        if (parent && &parent->get() == &removed) {
            skel->clear_parent_character();
        }
    }
    characters_.erase(it);

    invalidate_object_index();
    if (!ensure_object_index()) {
        throw std::runtime_error("removing character left duplicate object IDs");
    }
    return result::success;
}

sm::expected_const_character sm::project::character(const object_id& id) const {
    auto it = characters_.find(id);
    if (it == characters_.end()) {
        return std::unexpected(result::not_found);
    }
    return const_character_ref(std::as_const(*it->second));
}

sm::result sm::project::set_character_root_bone(const object_id& character_id,const object_id& bone_id) {
    auto character_it=characters_.find(character_id);
    if(character_it==characters_.end()) return result::not_found;
    if(bone_id.is_nil()) {
        character_it->second->set_character_root_bone({});
        return result::success;
    }
    auto bone=topology_.get<sm::bone>(bone_id);
    if(!bone) return result::not_found;
    auto parent=bone->get().owner().parent_character();
    if(!parent || parent->get().id()!=character_id) return result::invalid_membership;
    character_it->second->set_character_root_bone(bone_id);
    return result::success;
}

const sm::project::mutable_object& sm::project::get_mutable(const object_id& id) const {
    if (!ensure_object_index()) {
        throw std::runtime_error("project contains duplicate object IDs");
    }
    auto it = objects_.find(id);
    if (it == objects_.end()) {
        throw std::runtime_error("project object ID not found");
    }
    return it->second;
}

sm::mutable_project_object sm::project::get(const object_id& id) {
    return std::visit(overloaded{
        [](node_ref ref) -> mutable_project_object { return ref; },
        [](bone_ref ref) -> mutable_project_object { return ref; },
        [](skel_ref) -> mutable_project_object {
            throw std::runtime_error("project object does not support mutable lookup");
        },
        [](character_ref) -> mutable_project_object {
            throw std::runtime_error("project object does not support mutable lookup");
        }
    }, get_mutable(id));
}

void sm::project::rename(object_id id, std::string name) {
    std::visit([&name](auto ref) { ref->set_name(name); }, get_mutable(id));
}

sm::const_project_object sm::project::get(const object_id& id) const {
    return std::visit(
        overloaded{
            [](sm::node_ref ref) -> sm::const_project_object {
                return sm::const_node_ref(std::as_const(ref.get()));
            },
            [](sm::bone_ref ref) -> sm::const_project_object {
                return sm::const_bone_ref(std::as_const(ref.get()));
            },
            [](sm::skel_ref ref) -> sm::const_project_object {
                return sm::const_skel_ref(std::as_const(ref.get()));
            },
            [](sm::character_ref ref) -> sm::const_project_object {
                return sm::const_character_ref(std::as_const(ref.get()));
            }
        },
        get_mutable(id)
    );
}

bool sm::project::has_unique_object_ids() const {
    return ensure_object_index();
}

void sm::project::clear() {
    objects_.clear();
    topology_.clear();
    characters_.clear();
    object_index_dirty_ = false;
    next_character_name_ = 1;
}

std::expected<sm::project_buffer, sm::project_result> sm::project::serialize() const {
    if (!ensure_object_index()) {
        return std::unexpected(project_result::duplicate_object_id);
    }

    if (!has_consistent_membership()) {
        return std::unexpected(project_result::invalid_project_json);
    }

    try {
        for (auto character : this->characters())
            character->animation_data().validate(topology_, character->character_root_bone());
    } catch (const std::invalid_argument&) {
        return std::unexpected(project_result::invalid_project_json);
    }

    try {
        detail::package_writer package;
        json characters = json::array();
        for (auto character : this->characters()) {
            json skeletons = json::array();
            for (const auto& id : character->rig().skeleton_ids()) skeletons.push_back(id.to_string());
            auto art = detail::write_artwork(character->artwork(),
                "characters/" + character->id().to_string() + "/artwork/", package);
            characters.push_back({{"id", character->id().to_string()}, {"name", character->name()},
                {"skeletons", std::move(skeletons)}, {"root_bone", character->character_root_bone().to_string()},
                {"artwork", std::move(art)}, {"animation_data", animation_assets_to_json(character->animation_data())}});
        }
        json semantic_project{{"version", project_json_version}, {"topology", topology_.to_json()}, {"characters", std::move(characters)}};
        auto text = semantic_project.dump(4);
        package.add(std::string(project_json_name), {reinterpret_cast<const std::uint8_t*>(text.data()), text.size()});
        return package.finish();
    } catch (const std::invalid_argument&) { return std::unexpected(project_result::invalid_artwork); }
    catch (...) { return std::unexpected(project_result::archive_error); }
}

sm::project_result sm::project::deserialize(std::span<const std::uint8_t> buffer) {
    std::unique_ptr<detail::package_reader> package;
    try { package = std::make_unique<detail::package_reader>(buffer); }
    catch (...) { return project_result::invalid_archive; }
    if (!package->contains(std::string(project_json_name))) return project_result::missing_project_json;
    image_buffer project_json;
    try { project_json = package->read(std::string(project_json_name)); }
    catch (...) { return project_result::archive_error; }

    // Keep staged characters alive until after staged topology destruction on every early return,
    // mirroring project member lifetime ordering for skeleton parent references.
    character_tbl new_characters;
    sm::topology new_topology;
    std::size_t new_next_character_name = 1;
    try {
        std::vector<std::set<std::string>> object_keys;
        auto semantic_project = json::parse(project_json, [&object_keys](int, json::parse_event_t event, json& value) {
            if (event == json::parse_event_t::object_start) object_keys.emplace_back();
            if (event == json::parse_event_t::key && !object_keys.back().insert(value.get<std::string>()).second)
                throw std::invalid_argument("Duplicate JSON key");
            if (event == json::parse_event_t::object_end) object_keys.pop_back();
            return true;
        });
        const auto version = semantic_project.at("version").get<double>();
        if (version != project_json_version && version != 5.0 && version != 4.0) {
            return project_result::invalid_project_json;
        }
        if (new_topology.from_json(semantic_project.at("topology")) != result::success) {
            return project_result::invalid_project_json;
        }

        const auto& character_json = semantic_project.at("characters");
        if (!character_json.is_array()) {
            return project_result::invalid_project_json;
        }

        std::unordered_set<object_id> claimed_skeletons;
        for (const auto& entry : character_json) {
            auto parsed_character_id = object_id::from_string(entry.at("id").get<std::string>());
            if (!parsed_character_id) {
                return project_result::invalid_project_json;
            }
            const auto character_id = *parsed_character_id;
            const auto name = entry.at("name").get<std::string>();
            const auto& skeleton_json = entry.at("skeletons");
            if (!skeleton_json.is_array() || skeleton_json.empty()) {
                return project_result::invalid_project_json;
            }
            if (new_characters.contains(character_id)) {
                return project_result::duplicate_object_id;
            }

            sm::rig rig(*this);
            std::unordered_set<object_id> rig_members;
            for (const auto& skeleton_entry : skeleton_json) {
                auto parsed_skeleton_id = object_id::from_string(skeleton_entry.get<std::string>());
                if (!parsed_skeleton_id) {
                    return project_result::invalid_project_json;
                }
                const auto skeleton_id = *parsed_skeleton_id;
                if (!rig_members.insert(skeleton_id).second ||
                    !claimed_skeletons.insert(skeleton_id).second) {
                    return project_result::invalid_project_json;
                }
                if (!new_topology.skeleton(skeleton_id)) {
                    return project_result::invalid_project_json;
                }
                rig.add_skeleton(skeleton_id);
            }

            object_id character_root_bone;
            if(version==project_json_version) {
                auto parsed_root=object_id::from_string(entry.at("root_bone").get<std::string>());
                if(!parsed_root) return project_result::invalid_project_json;
                character_root_bone=*parsed_root;
            } else {
                character_root_bone=default_character_root_bone(new_topology,rig.skeleton_ids());
            }
            if(!character_root_bone.is_nil()) {
                auto root_bone=new_topology.get<sm::bone>(character_root_bone);
                if(!root_bone || !rig_members.contains(root_bone->get().owner().id()))
                    return project_result::invalid_project_json;
            }

            auto character = sm::character::make_unique(
                *this, character_id, name, std::move(rig), character_root_bone);
            if (version == project_json_version && !entry.contains("artwork")) return project_result::invalid_artwork;
            if (entry.contains("artwork")) {
                try { character->artwork_ = detail::read_artwork(entry.at("artwork"),
                    "characters/" + character_id.to_string() + "/artwork/", *package); }
                catch (...) { return project_result::invalid_artwork; }
            }
            if (entry.contains("animation_data")) character->animation_data_ = animation_assets_from_json(entry.at("animation_data"));
            else initialize_animation_assets(character->animation_data_, new_topology, character->rig().skeleton_ids());
            auto* character_ptr = character.get();
            new_characters.emplace(character_id, std::move(character));
            for (const auto& skeleton_id : character_ptr->rig().skeleton_ids()) {
                new_topology.skeleton(skeleton_id)->get().set_parent_character(*character_ptr);
            }

            constexpr std::string_view prefix = "character-";
            if (name.starts_with(prefix)) {
                const auto suffix = std::string_view(name).substr(prefix.size());
                std::size_t value = 0;
                const auto [ptr, ec] = std::from_chars(suffix.data(), suffix.data() + suffix.size(), value);
                if (ec == std::errc{} && ptr == suffix.data() + suffix.size() &&
                    value < std::numeric_limits<std::size_t>::max()) {
                    new_next_character_name = std::max(new_next_character_name, value + 1);
                }
            }
        }

        for (const auto& [id, character] : new_characters)
            character->animation_data().validate(new_topology, character->character_root_bone());
    }
    catch (...) {
        return project_result::invalid_project_json;
    }

    std::unordered_map<object_id, mutable_object> new_objects;
    if (!build_object_index(new_topology, new_characters, new_objects)) {
        return project_result::duplicate_object_id;
    }

    // Commit only after topology, character membership and the project-wide object namespace
    // have all validated successfully. The old characters remain alive while old topology is
    // destroyed, and the staged character objects keep stable addresses across the map move.
    objects_.clear();
    topology_ = std::move(new_topology);
    characters_ = std::move(new_characters);
    objects_ = std::move(new_objects);
    object_index_dirty_ = false;
    next_character_name_ = new_next_character_name;
    assert(has_consistent_membership());
    return project_result::success;
}

sm::artwork& sm::project::artwork(const object_id& id) { return characters_.at(id)->artwork_; }
const sm::artwork& sm::project::artwork(const object_id& id) const { return characters_.at(id)->artwork_; }
bool sm::project::slot_resolved(const object_id& id, const std::string& slot) const {
    const auto bone_id = artwork(id).slot_definitions().at(slot).bone;
    if (!ensure_object_index()) return false;
    auto it = objects_.find(bone_id);
    if (it == objects_.end()) return false;
    auto bone = std::get_if<bone_ref>(&it->second);
    if (!bone) return false;
    auto parent = bone->get().owner().parent_character();
    return parent && parent->get().id() == id;
}

std::vector<sm::resolved_sprite> sm::project::resolve_artwork(const object_id& id,
    const std::string& appearance_name, const std::map<std::string, std::string>& states, const sm::topology* geometry) const {
    const auto& art = artwork(id);
    const auto& appearance = art.appearances().at(appearance_name);
    std::vector<resolved_sprite> sprites;
    sprites.reserve(appearance.appearance_slots.size());
    for (const auto& slot : appearance.appearance_slots) {
        if (!slot_resolved(id, slot.slot)) continue;
        const auto state = states.find(slot.slot);
        const auto target = art.resolve_frame(appearance_name, slot.slot,
            state == states.end() ? "default" : state->second);
        if (!target) continue;

        const auto& definition = art.slot_definitions().at(slot.slot);
        auto resolved_bone = (geometry ? *geometry : topology_).get<sm::bone>(definition.bone);
        if (!resolved_bone) continue;
        const auto& bone = resolved_bone->get();
        const auto anchor = definition.anchor == bone_anchor::root
            ? bone.parent_node().world_pos() : bone.child_node().world_pos();
        const matrix bone_transform = translation_matrix(anchor) * rotation_matrix(bone.world_rotation());
        const auto& frame = art.frames().at(*target);
        const auto& local = slot.transform;
        const matrix transform = bone_transform
            * translation_matrix(local.translation)
            * rotation_matrix(local.rotation)
            * scale_matrix(local.scale.x, local.scale.y)
            * translation_matrix(-frame.registration_origin);
        sprites.push_back({slot.slot, *target, frame.image, frame.registration_origin, transform, bone_transform});
    }
    return sprites;
}

sm::animation_assets& sm::project::animation_data(const object_id& id) { return characters_.at(id)->animation_data_; }
const sm::animation_assets& sm::project::animation_data(const object_id& id) const { return characters_.at(id)->animation_data_; }
