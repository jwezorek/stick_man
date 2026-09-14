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
    constexpr double project_json_version = 5.0;

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
    detach_skeleton(skel->get());
    auto deleted = topology_.delete_skeleton(id);
    if (deleted != result::success) {
        return deleted;
    }
    prune_empty_characters();
    invalidate_object_index();
    if (!ensure_object_index()) {
        throw std::runtime_error("deleting skeleton left duplicate object IDs");
    }
    assert(has_consistent_membership());
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

sm::result sm::project::can_create_bone(const node& u, const node& v) const {
    if (&u.owner().owner() != &topology_ || &v.owner().owner() != &topology_)
        return result::foreign_skeleton;
    const auto a = u.owner().parent_character(), b = v.owner().parent_character();
    if (a && b && a->get().id() != b->get().id()) return result::different_characters;
    if (&u.owner() == &v.owner()) return result::cyclic_bones;
    if (!v.is_root()) return result::multi_parent_node;
    return result::success;
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
    }
    invalidate_object_index();
    if (!ensure_object_index()) {
        throw std::runtime_error("creating bone produced duplicate object IDs");
    }
    assert(has_consistent_membership());
    return created;
}

sm::topology_change sm::project::replace_skeletons(
        const std::vector<object_id>& replacees,
        const std::vector<skel_ref>& replacements,
        const std::unordered_set<object_id>& regenerate_ids,
        const membership_state* restored_membership) {
    topology_change change;
    auto plan = plan_replacement(replacees, replacements, restored_membership);
    if (!plan) { change.status = plan.error(); return change; }
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
            return {{}, {}, copied.error()};
        }
        // Artwork follows surviving bones when replacement assigns fresh IDs.
        // Apply only this component's bone remap to its owning character's staged
        // semantic state; unresolved references to deleted bones remain intact.
        const auto parent = plan->membership.parents.at(replacement->id());
        if (parent) {
            std::unordered_map<object_id, object_id> bone_remap;
            for (auto bone : replacement->bones())
                if (auto it = id_remap.find(bone->id()); it != id_remap.end()) bone_remap.emplace(*it);
            for (auto& state : plan->membership.characters)
                if (state.id == *parent) state.artwork.remap_bones(bone_remap);
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
            sm::character::make_unique(*this, state.id, state.name, sm::rig(*this)));
        characters_.at(state.id)->artwork_ = state.artwork;
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
    assert(has_consistent_membership());
    return change;
}

sm::membership_state sm::project::snapshot_membership(const std::vector<object_id>& ids) const {
    membership_state state;
    std::unordered_set<object_id> seen;
    for (const auto& id : ids) {
        auto skel = topology_.skeleton(id);
        if (!skel) continue;
        auto parent = skel->get().parent_character();
        state.parents[id] = parent ? std::optional(parent->get().id()) : std::nullopt;
        if (parent && seen.insert(parent->get().id()).second)
            state.characters.push_back({parent->get().id(), parent->get().name(), parent->get().artwork()});
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
            prepared.emplace(c.id, sm::character::make_unique(*this, c.id, c.name, sm::rig(*this)));
            prepared.at(c.id)->artwork_ = c.artwork;
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
    assert(has_consistent_membership());
    return result::success;
}

std::expected<sm::replacement_plan, sm::result> sm::project::plan_replacement(
        const std::vector<object_id>& replacees, const std::vector<skel_ref>& replacements,
        const membership_state* restored) const {
    replacement_plan plan;
    std::unordered_set<object_id> removed, affected_characters;
    std::unordered_map<object_id, std::optional<object_id>> node_parents;
    for (const auto& id : replacees) {
        if (!removed.insert(id).second) return std::unexpected(result::duplicate_skeleton);
        auto skel = topology_.skeleton(id);
        if (!skel) return std::unexpected(result::not_found);
        auto parent = skel->get().parent_character();
        std::optional<object_id> cid = parent ? std::optional(parent->get().id()) : std::nullopt;
        if (cid) affected_characters.insert(*cid);
        for (auto node : skel->get().nodes()) node_parents.emplace(node->id(), cid);
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
    for (const auto& id : affected_characters) {
        const auto& rig = characters_.at(id)->rig();
        bool survives = std::ranges::any_of(rig.skeleton_ids(), [&](const auto& sid) { return !removed.contains(sid); });
        for (const auto& [sid, parent] : plan.membership.parents) survives |= parent == id;
        if (!survives) plan.deleted_character_ids.push_back(id);
    }
    return plan;
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

    auto name = "character-" + std::to_string(next_character_name_);
    auto created = sm::character::make_unique(*this, id, std::move(name), std::move(rig));
    auto* created_ptr = created.get();
    auto [it, inserted] = characters_.emplace(id, std::move(created));
    if (!inserted) {
        return std::unexpected(result::duplicate_id);
    }
    for (auto skel : validated) {
        skel->set_parent_character(*created_ptr);
    }
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
        detail::package_writer package;
        json characters = json::array();
        for (auto character : this->characters()) {
            json skeletons = json::array();
            for (const auto& id : character->rig().skeleton_ids()) skeletons.push_back(id.to_string());
            auto art = detail::write_artwork(character->artwork(),
                "characters/" + character->id().to_string() + "/artwork/", package);
            characters.push_back({{"id", character->id().to_string()}, {"name", character->name()},
                {"skeletons", std::move(skeletons)}, {"artwork", std::move(art)}});
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
        if (version != project_json_version && version != 4.0) {
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

            auto character = sm::character::make_unique(
                *this, character_id, name, std::move(rig));
            if (version == project_json_version && !entry.contains("artwork")) return project_result::invalid_artwork;
            if (entry.contains("artwork")) {
                try { character->artwork_ = detail::read_artwork(entry.at("artwork"),
                    "characters/" + character_id.to_string() + "/artwork/", *package); }
                catch (...) { return project_result::invalid_artwork; }
            }
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
    const std::string& appearance_name, const std::map<std::string, std::string>& states) const {
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
        const auto& bone = std::get<bone_ref>(objects_.at(definition.bone)).get();
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
