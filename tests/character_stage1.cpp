#include "core/sm_project.hpp"

#include <algorithm>
#include <iostream>
#include <ranges>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace {

static_assert(std::is_same_v<
    decltype(std::declval<const sm::project&>().topology()), const sm::topology&>);
static_assert(std::is_same_v<
    decltype(std::declval<const sm::character&>().rig()), const sm::rig&>);
static_assert(std::variant_size_v<sm::mutable_project_object> == 2);
static_assert(!std::is_constructible_v<sm::mutable_project_object, sm::character_ref>);
static_assert(std::is_constructible_v<sm::const_project_object, sm::const_character_ref>);

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

std::size_t character_count(const sm::project& project) {
    return static_cast<std::size_t>(std::ranges::distance(project.characters()));
}

bool contains_skeleton(const std::vector<sm::const_skel_ref>& skeletons, const sm::skeleton& expected) {
    return std::ranges::any_of(skeletons, [&](auto skel) { return &skel.get() == &expected; });
}

void require_membership_invariant(const sm::project& project) {
    for (auto character : project.characters()) {
        for (auto skeleton : character->rig().skeletons()) {
            require(skeleton->parent_character().has_value(), "rig member has no parent character");
            require(&skeleton->parent_character()->get() == &character.get(),
                "rig member parent disagrees with rig owner");
            require(character->rig().contains(skeleton->id()), "resolved rig member ID is absent from rig");
        }
    }

    for (auto skeleton : project.topology().skeletons()) {
        auto parent = skeleton->parent_character();
        if (parent) {
            require(parent->get().rig().contains(skeleton->id()),
                "skeleton parent disagrees with rig membership");
            auto resolved_parent = project.character(parent->get().id());
            require(resolved_parent.has_value() && &resolved_parent->get() == &parent->get(),
                "skeleton parent does not resolve to the owning project character");
        }
    }
}

void creates_character_from_one_loose_skeleton() {
    sm::project project;
    auto& skeleton = project.create_skeleton({0.0, 0.0});

    std::vector<sm::const_skel_ref> members{sm::const_skel_ref(skeleton)};
    auto created = project.create_character(members);

    require(created.has_value(), "single-skeleton character creation failed");
    require(character_count(project) == 1, "project did not retain created character");
    require(created->get().name() == "character-1", "unexpected default character name");
    require(created->get().rig().size() == 1, "single-skeleton rig has wrong size");
    require(!skeleton.is_loose(), "member skeleton still reports loose");
    require(skeleton.parent_character().has_value(), "member skeleton has no parent character");
    require(skeleton.parent_character()->get().id() == created->get().id(), "wrong parent character");
    require_membership_invariant(project);
}

void creates_character_from_disconnected_skeletons_and_resolves_rig() {
    sm::project project;
    auto& first = project.create_skeleton({0.0, 0.0});
    auto& second = project.create_skeleton({10.0, 10.0});
    auto& third = project.create_skeleton({20.0, 20.0});

    std::vector<sm::const_skel_ref> members{sm::const_skel_ref(first), sm::const_skel_ref(second), sm::const_skel_ref(third)};
    auto created = project.create_character(members);

    require(created.has_value(), "multi-skeleton character creation failed");
    require(created->get().rig().size() == 3, "multi-skeleton rig has wrong size");
    auto resolved = created->get().rig().skeletons();
    require(resolved.size() == 3, "rig did not resolve every member");
    require(contains_skeleton(resolved, first), "rig did not resolve first skeleton");
    require(contains_skeleton(resolved, second), "rig did not resolve second skeleton");
    require(contains_skeleton(resolved, third), "rig did not resolve third skeleton");
    for (auto* skeleton : {&first, &second, &third}) {
        require(skeleton->parent_character().has_value(), "rig member has no parent character");
        require(skeleton->parent_character()->get().id() == created->get().id(), "rig member has wrong parent character");
    }
    require_membership_invariant(project);
}

void character_ids_are_stable_and_project_wide_unique() {
    sm::project project;
    auto& first = project.create_skeleton({0.0, 0.0});
    auto& second = project.create_skeleton({10.0, 10.0});
    auto first_node_id = first.root_node().id();
    auto first_skeleton_id = first.id();
    auto second_skeleton_id = second.id();

    std::vector<sm::const_skel_ref> first_members{sm::const_skel_ref(first)};
    std::vector<sm::const_skel_ref> second_members{sm::const_skel_ref(second)};
    auto first_character = project.create_character(first_members);
    auto second_character = project.create_character(second_members);
    require(first_character && second_character, "character fixture creation failed");

    auto stable_id = first_character->get().id();
    project.rename(stable_id, "renamed");
    require(first_character->get().id() == stable_id, "rename changed character identity");
    require(stable_id != second_character->get().id(), "character IDs collided");
    require(stable_id != first_node_id, "character ID collided with node ID");
    require(stable_id != first_skeleton_id && stable_id != second_skeleton_id,
        "character ID collided with skeleton ID");

    auto& loose = project.create_skeleton({20.0, 20.0});
    auto colliding_bone = project.create_bone(stable_id, "collision", first.root_node(), loose.root_node());
    require(!colliding_bone && colliding_bone.error() == sm::result::duplicate_id,
        "character ID was not reserved in the project-wide namespace");
    require(loose.is_loose(), "duplicate-ID rejection changed loose topology membership");

    require(project.has_unique_object_ids(), "project-wide object ID uniqueness failed");
    require_membership_invariant(project);
}

void duplicate_character_names_are_allowed_and_generic_lookup_and_rename_work() {
    sm::project project;
    auto& first = project.create_skeleton({0.0, 0.0});
    auto& second = project.create_skeleton({10.0, 10.0});
    std::vector<sm::const_skel_ref> first_members{sm::const_skel_ref(first)};
    std::vector<sm::const_skel_ref> second_members{sm::const_skel_ref(second)};
    auto first_character = project.create_character(first_members);
    auto second_character = project.create_character(second_members);
    require(first_character && second_character, "character fixture creation failed");

    project.rename(first_character->get().id(), "Fred");
    project.rename(second_character->get().id(), "Fred");
    require(first_character->get().name() == "Fred" && second_character->get().name() == "Fred",
        "duplicate character labels were not allowed");

    const auto& const_project = std::as_const(project);
    auto generic = const_project.get(first_character->get().id());
    require(std::holds_alternative<sm::const_character_ref>(generic), "generic lookup did not return character");
    require(std::get<sm::const_character_ref>(generic)->id() == first_character->get().id(),
        "generic lookup returned wrong character");
    auto direct = const_project.character(first_character->get().id());
    require(direct.has_value() && &direct->get() == &first_character->get(),
        "direct character lookup returned wrong character");

    bool mutable_lookup_rejected = false;
    try {
        (void)project.get(first_character->get().id());
    } catch (const std::runtime_error&) {
        mutable_lookup_rejected = true;
    }
    require(mutable_lookup_rejected, "character leaked into mutable generic project lookup");
    require_membership_invariant(project);
}

void rejects_invalid_membership_without_mutating_project() {
    sm::project project;
    sm::project other_project;
    auto& local = project.create_skeleton({0.0, 0.0});
    auto& other_local = project.create_skeleton({5.0, 5.0});
    auto& foreign = other_project.create_skeleton({10.0, 10.0});

    std::vector<sm::const_skel_ref> empty;
    auto empty_result = project.create_character(empty);
    require(!empty_result && empty_result.error() == sm::result::empty_character,
        "empty rig was not rejected with empty_character");
    require(character_count(project) == 0, "empty-rig failure mutated character collection");

    std::vector<sm::const_skel_ref> duplicate{sm::const_skel_ref(local), sm::const_skel_ref(local)};
    auto duplicate_result = project.create_character(duplicate);
    require(!duplicate_result && duplicate_result.error() == sm::result::duplicate_skeleton,
        "duplicate membership was not rejected with duplicate_skeleton");
    require(character_count(project) == 0, "duplicate-membership failure mutated character collection");
    require(local.is_loose(), "duplicate-membership failure changed skeleton parent");

    std::vector<sm::const_skel_ref> foreign_members{sm::const_skel_ref(local), sm::const_skel_ref(foreign)};
    auto foreign_result = project.create_character(foreign_members);
    require(!foreign_result && foreign_result.error() == sm::result::foreign_skeleton,
        "foreign skeleton was not rejected with foreign_skeleton");
    require(character_count(project) == 0, "foreign-membership failure mutated character collection");
    require(local.is_loose(), "foreign-membership failure changed local skeleton parent");
    require(foreign.is_loose(), "foreign-membership failure changed foreign skeleton parent");

    std::vector<sm::const_skel_ref> owned_members{sm::const_skel_ref(local)};
    auto owner = project.create_character(owned_members);
    require(owner.has_value(), "owned-skeleton fixture creation failed");
    auto owner_id = owner->get().id();

    std::vector<sm::const_skel_ref> second_attempt{sm::const_skel_ref(local), sm::const_skel_ref(other_local)};
    auto already_owned = project.create_character(second_attempt);
    require(!already_owned && already_owned.error() == sm::result::skeleton_already_owned,
        "already-owned skeleton was not rejected with skeleton_already_owned");
    require(character_count(project) == 1, "already-owned failure created a partial character");
    require(local.parent_character() && local.parent_character()->get().id() == owner_id,
        "already-owned failure changed existing parent");
    require(other_local.is_loose(), "already-owned failure partially adopted a loose skeleton");
    require_membership_invariant(project);
}

void removing_character_clears_every_parent_reference() {
    sm::project project;
    auto& first = project.create_skeleton({0.0, 0.0});
    auto& second = project.create_skeleton({10.0, 10.0});
    std::vector<sm::const_skel_ref> members{sm::const_skel_ref(first), sm::const_skel_ref(second)};
    auto created = project.create_character(members);
    require(created.has_value(), "character fixture creation failed");
    auto id = created->get().id();

    require(project.remove_character(id) == sm::result::success, "character removal failed");
    require(character_count(project) == 0, "removed character still enumerates");
    require(first.is_loose() && second.is_loose(), "character removal left dangling skeleton parent");
    require(!project.character(id), "removed character still resolves by ID");
    require(project.has_unique_object_ids(), "object index invalid after character removal");
    require_membership_invariant(project);
}

} // namespace

int main() {
    try {
        creates_character_from_one_loose_skeleton();
        creates_character_from_disconnected_skeletons_and_resolves_rig();
        character_ids_are_stable_and_project_wide_unique();
        duplicate_character_names_are_allowed_and_generic_lookup_and_rename_work();
        rejects_invalid_membership_without_mutating_project();
        removing_character_clears_every_parent_reference();
        std::cout << "PASS character_stage1\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
