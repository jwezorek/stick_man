#include "model/project.hpp"
#include "core/sm_animation.hpp"
#include "json.hpp"

#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}

struct fixture {
    mdl::project project;
    sm::object_id skeleton;
    sm::object_id root;
    sm::object_id character;
    sm::object_id animation;

    fixture() {
        auto& skel = project.core().create_skeleton({10, 20});
        skeleton = skel.id();
        root = skel.root_node().id();
        std::vector<sm::const_skel_ref> members{skel};
        character = project.core().create_character(members).value()->id();
        sm::animation a;
        a.name = "Walk";
        animation = a.id;
        project.core().animation_data(character).animations.push_back(a);
        require(project.core().validate_integrity() == sm::result::success, "fixture is invalid");
    }
};

void standalone_poses_and_empty_animation_assets() {
    fixture f;
    auto& assets = f.project.core().animation_data(f.character);
    require(assets.poses.size() == 1, "Default Pose was not preserved");
    require(assets.find_pose(assets.default_pose) != nullptr, "Default Pose identity is invalid");

    auto root = f.project.core().get(f.root);
    std::get<sm::node_ref>(root)->set_world_pos({40, 50});
    auto named = sm::capture_pose(f.project.core().topology(), {f.skeleton}, "Standing");
    assets.poses.push_back(named);
    require(sm::pose_compatible(assets.poses.back(), f.project.core().topology(), {f.skeleton}),
        "named pose does not remain compatible with its rig");

    auto json = sm::animation_assets_to_json(assets);
    require(json.contains("animations"), "non-empty animation collection was omitted");
    require(json["animations"].size() == 1, "animation count did not serialize");
    require(json["animations"][0].size() == 2 && json["animations"][0].contains("id") &&
        json["animations"][0].contains("name"), "Animation V2 asset serialized fields beyond identity/name");

    auto round_trip = sm::animation_assets_from_json(json);
    require(round_trip.find_pose(named.id) != nullptr, "named pose did not round-trip");
    require(round_trip.find_animation(f.animation) != nullptr, "empty Animation V2 asset did not round-trip");

    auto no_animations = assets;
    no_animations.animations.clear();
    auto empty_json = sm::animation_assets_to_json(no_animations);
    require(!empty_json.contains("animations"), "empty animation collection should be omitted");
    auto missing_member = sm::animation_assets_from_json(empty_json);
    require(missing_member.animations.empty(), "missing animations member did not load as empty");

    auto duplicate = *assets.find_animation(f.animation);
    duplicate.id = sm::object_id::generate();
    duplicate.name += " copy";
    assets.animations.push_back(duplicate);
    require(assets.find_animation(duplicate.id) != nullptr, "animation duplicate failed");
    assets.find_animation(f.animation); // original identity remains distinct
    std::erase_if(assets.animations, [&](const auto& a) { return a.id == duplicate.id; });
    require(assets.find_animation(duplicate.id) == nullptr, "animation delete failed");
}

void animation_session_is_isolated() {
    fixture f;
    f.project.rename(f.character, "Hero");
    f.project.mark_saved();
    require(!f.project.is_dirty(), "mark_saved did not clean document");
    require(f.project.can_undo(), "document history fixture missing");

    const auto persistent_before = f.project.core().topology().get<sm::node>(f.root)->get().world_pos();
    require(f.project.begin_animation_session(f.character, f.animation) == sm::result::success,
        "could not enter Animation Mode session");
    require(f.project.animation_mode(), "Animation Mode session not active");
    require(!f.project.can_undo() && !f.project.can_redo(), "session inherited document undo/redo state");
    require(&f.project.topology().get<sm::node>(f.root)->get() !=
        &f.project.core().topology().get<sm::node>(f.root)->get(), "working topology is not detached");

    const sm::point moved{75, 90};
    const sm::point moved_again{105, 120};
    f.project.transform_node_positions({{f.root, persistent_before}}, {{f.root, moved}});
    f.project.transform_node_positions({{f.root, moved}}, {{f.root, moved_again}});
    require(f.project.topology().get<sm::node>(f.root)->get().world_pos() == moved_again,
        "session pose edits did not change working model");
    require(f.project.core().topology().get<sm::node>(f.root)->get().world_pos() == persistent_before,
        "session pose edit changed persistent topology");
    require(!f.project.is_dirty(), "session pose edit marked document dirty");
    require(f.project.can_undo(), "session pose edit was not undoable");

    f.project.undo();
    require(f.project.topology().get<sm::node>(f.root)->get().world_pos() == moved,
        "first session undo did not restore the previous working pose");
    f.project.undo();
    require(f.project.topology().get<sm::node>(f.root)->get().world_pos() == persistent_before,
        "second session undo did not restore the initial working pose");
    require(f.project.can_redo(), "session undo did not populate session redo");
    require(f.project.redo() == sm::result::success, "first session redo failed");
    require(f.project.topology().get<sm::node>(f.root)->get().world_pos() == moved,
        "first session redo did not restore the intermediate working pose");
    require(f.project.redo() == sm::result::success, "second session redo failed");
    require(f.project.topology().get<sm::node>(f.root)->get().world_pos() == moved_again,
        "second session redo did not restore the final working pose");

    auto saved_while_editing = f.project.serialize();
    require(saved_while_editing.has_value(), "save while Animation Mode is active failed");
    sm::project loaded;
    require(loaded.deserialize(*saved_while_editing) == sm::project_result::success,
        "saved persistent project did not reload");
    require(loaded.topology().get<sm::node>(f.root)->get().world_pos() == persistent_before,
        "scratch pose leaked into serialization");
    const auto& loaded_assets = loaded.character(f.character)->get().animation_data();
    require(loaded_assets.find_animation(f.animation) != nullptr,
        "empty animation asset identity was not serialized");

    f.project.end_animation_session();
    require(!f.project.animation_mode(), "Animation Mode session did not end");
    require(f.project.core().topology().get<sm::node>(f.root)->get().world_pos() == persistent_before,
        "leaving Animation Mode changed persistent pose");
    require(f.project.can_undo(), "document undo history was not restored after Animation Mode");
    f.project.undo();
    require(f.project.core().character(f.character)->get().name() != "Hero",
        "document undo history was replaced by session history");
}
}

int main() {
    try {
        standalone_poses_and_empty_animation_assets();
        animation_session_is_isolated();
        std::cout << "PASS Animation V2 Phase 1 core/model\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
