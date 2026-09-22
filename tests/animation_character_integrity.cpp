#include "core/sm_project.hpp"
#include "core/sm_animation.hpp"

#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

template<class Fn>
void require_invalid(Fn&& fn, const char* message) {
    bool rejected = false;
    try { fn(); }
    catch (const std::invalid_argument&) { rejected = true; }
    require(rejected, message);
}

struct chain_ids {
    sm::object_id skeleton;
    sm::object_id root;
    sm::object_id tip;
    sm::object_id bone;
};

chain_ids make_chain(sm::project& project, double y) {
    auto& root = project.create_skeleton({0.0, y});
    auto& tip = project.create_skeleton({10.0, y});
    const auto root_id = root.root_node().id();
    const auto tip_id = tip.root_node().id();
    auto bone = project.create_bone("bone", root.root_node(), tip.root_node());
    require(bone.has_value(), "failed to create chain bone");
    return {bone->get().owner().id(), root_id, tip_id, bone->get().id()};
}

sm::object_id make_character(sm::project& project, std::initializer_list<sm::object_id> skeleton_ids) {
    std::vector<sm::const_skel_ref> skeletons;
    for (const auto id : skeleton_ids) skeletons.push_back(*project.topology().skeleton(id));
    auto character = project.create_character(skeletons);
    require(character.has_value(), "failed to create character");
    return character->get().id();
}

sm::animation_action rigid_translation(sm::object_id skeleton,
        sm::translation_reference reference = sm::translation_reference::animation_root,
        sm::object_id reference_bone = {}) {
    sm::animation_action action;
    action.duration = 100;
    sm::rigid_translation data;
    data.skeletons = {skeleton};
    data.path = sm::motion_path(sm::line_path{{0.0, 0.0}, {5.0, 0.0}});
    data.reference = reference;
    data.reference_bone = reference_bone;
    action.data = std::move(data);
    return action;
}

void append_animation(sm::animation_assets& assets, std::vector<sm::animation_layer> layers) {
    sm::animation animation;
    animation.name = "integrity";
    animation.base_pose = assets.default_pose;
    animation.layers = std::move(layers);
    assets.animations.push_back(std::move(animation));
}

void character_scoped_references_are_rejected() {
    sm::project project;
    const auto a = make_chain(project, 0.0);
    const auto b = make_chain(project, 20.0);
    const auto char_a = make_character(project, {a.skeleton});
    const auto char_b = make_character(project, {b.skeleton});
    (void)char_b;
    const auto& character = project.character(char_a)->get();

    auto expect_bad_action = [&](sm::animation_action action, const char* message) {
        auto assets = character.animation_data();
        append_animation(assets, {{{std::move(action)}}});
        require_invalid([&] { assets.validate(project.topology(), character.rig().skeleton_ids(),
            character.character_root_bone()); }, message);
    };

    sm::animation_action external_rotation;
    external_rotation.data = sm::rigid_rotation{b.bone, sm::rotation_pivot::root, 0.25,
        sm::rotation_propagation::hierarchy};
    expect_bad_action(external_rotation, "foreign rotation bone was accepted");

    expect_bad_action(rigid_translation(b.skeleton), "foreign rigid-translation skeleton was accepted");

    sm::animation_action external_effector;
    sm::ik_translation ik_effector;
    ik_effector.effector = b.tip;
    ik_effector.path = sm::motion_path(sm::line_path{{0.0, 0.0}, {1.0, 0.0}});
    external_effector.data = ik_effector;
    expect_bad_action(external_effector, "foreign IK effector was accepted");

    sm::animation_action external_pin;
    sm::ik_translation ik_pin;
    ik_pin.effector = a.tip;
    ik_pin.pins = {b.root};
    ik_pin.path = sm::motion_path(sm::line_path{{0.0, 0.0}, {1.0, 0.0}});
    external_pin.data = ik_pin;
    expect_bad_action(external_pin, "foreign IK pin was accepted");

    expect_bad_action(rigid_translation(a.skeleton, sm::translation_reference::bone, b.bone),
        "foreign Bone reference frame was accepted");

    auto assets = character.animation_data();
    assets.find_pose(assets.default_pose); // keep the fixture intent explicit
    assets.poses.front().node_positions.emplace(b.root, sm::point{0.0, 0.0});
    require_invalid([&] { assets.validate(project.topology(), character.rig().skeleton_ids(),
        character.character_root_bone()); }, "pose node outside the character rig was accepted");

    // The project-level seam must use the same scoped definition of validity.
    project.animation_data(char_a) = character.animation_data();
    append_animation(project.animation_data(char_a), {{{external_rotation}}});
    require(project.validate_integrity() == sm::result::invalid_animation,
        "project integrity seam accepted a cross-character action");
}

void character_root_is_transactional_and_semantic() {
    sm::project project;
    const auto a = make_chain(project, 0.0);
    const auto b = make_chain(project, 20.0);
    const auto character_id = make_character(project, {a.skeleton, b.skeleton});
    auto& assets = project.animation_data(character_id);

    auto relative = rigid_translation(a.skeleton, sm::translation_reference::character_root);
    append_animation(assets, {{{relative}}});
    require(project.validate_integrity() == sm::result::success, "valid root-relative animation rejected");

    // With only the translation present, either rig bone is a usable Character Root.
    require(project.set_character_root_bone(character_id, b.bone) == sm::result::success,
        "valid root change was rejected");
    require(project.character(character_id)->get().character_root_bone() == b.bone,
        "valid root change did not commit");
    require(project.set_character_root_bone(character_id, a.bone) == sm::result::success,
        "restoring valid root was rejected");

    sm::animation_action later_rotation;
    later_rotation.data = sm::rigid_rotation{b.bone, sm::rotation_pivot::root, 0.5,
        sm::rotation_propagation::hierarchy};
    assets.animations.front().layers.push_back({{later_rotation}});
    require(project.validate_integrity() == sm::result::success,
        "fixture was invalid before root-change test");

    const auto before_root = project.character(character_id)->get().character_root_bone();
    require(project.set_character_root_bone(character_id, b.bone) == sm::result::invalid_animation,
        "root change that invalidates action ordering was accepted");
    require(project.character(character_id)->get().character_root_bone() == before_root,
        "failed root change mutated the character");
    require(project.validate_integrity() == sm::result::success,
        "failed root change left the project invalid");

    require(project.set_character_root_bone(character_id, {}) == sm::result::invalid_animation,
        "root was cleared while a Character Root-relative action still requires it");

    const auto foreign = make_chain(project, 40.0);
    make_character(project, {foreign.skeleton});
    require(project.set_character_root_bone(character_id, foreign.bone) == sm::result::invalid_membership,
        "foreign character root bone was accepted");
    require(project.character(character_id)->get().character_root_bone() == before_root,
        "foreign root attempt changed the character root");
}

void missing_root_is_rejected_when_a_root_frame_is_required() {
    sm::project project;
    auto& single = project.create_skeleton({0.0, 0.0});
    const auto skeleton_id = single.id();
    const auto character_id = make_character(project, {skeleton_id});
    require(project.character(character_id)->get().character_root_bone().is_nil(),
        "single-node character unexpectedly has a root bone");

    auto& assets = project.animation_data(character_id);
    append_animation(assets, {{{rigid_translation(skeleton_id, sm::translation_reference::character_root)}}});
    require(project.validate_integrity() == sm::result::invalid_animation,
        "Character Root-relative action accepted an unusable root");
}

void structural_semantic_change_is_rejected_before_commit() {
    sm::project project;
    const auto a = make_chain(project, 0.0);
    const auto b = make_chain(project, 20.0);
    const auto character_id = make_character(project, {a.skeleton, b.skeleton});
    auto& assets = project.animation_data(character_id);

    auto relative = rigid_translation(a.skeleton, sm::translation_reference::character_root);
    sm::animation_action later_rotation;
    later_rotation.data = sm::rigid_rotation{b.bone, sm::rotation_pivot::root, 0.5,
        sm::rotation_propagation::hierarchy};
    append_animation(assets, {{{relative}}, {{later_rotation}}});
    require(project.validate_integrity() == sm::result::success, "merge fixture begins invalid");

    const auto before = project.topology().to_json_str();
    auto a_tip = project.topology().get<sm::node>(a.tip);
    auto b_root = project.topology().get<sm::node>(b.root);
    require(a_tip && b_root, "merge endpoints missing");
    auto merged = project.create_bone("join", *a_tip, *b_root);
    require(!merged && merged.error() == sm::result::invalid_animation,
        "semantic write-scope/order change was not rejected");
    require(project.topology().to_json_str() == before,
        "rejected structural edit mutated live topology");
    require(project.topology().skeleton(a.skeleton).has_value() &&
        project.topology().skeleton(b.skeleton).has_value(),
        "rejected structural edit changed skeleton membership");
    require(project.validate_integrity() == sm::result::success,
        "rejected structural edit left the project invalid");
}

void deletion_cleanup_still_preserves_integrity() {
    sm::project project;
    const auto a = make_chain(project, 0.0);
    const auto b = make_chain(project, 20.0);
    const auto character_id = make_character(project, {a.skeleton, b.skeleton});
    auto& assets = project.animation_data(character_id);

    sm::animation_action rotation;
    rotation.data = sm::rigid_rotation{b.bone, sm::rotation_pivot::root, 0.5,
        sm::rotation_propagation::hierarchy};
    const auto removed_action = rotation.id;
    append_animation(assets, {{{rotation}}});
    require(project.validate_integrity() == sm::result::success, "delete fixture begins invalid");

    require(project.delete_skeleton(b.skeleton) == sm::result::success, "skeleton deletion failed");
    const auto& after = project.animation_data(character_id);
    bool action_found = false;
    for (const auto& animation : after.animations) for (const auto& layer : animation.layers)
        for (const auto& action : layer.actions) action_found |= action.id == removed_action;
    require(!action_found, "deletion did not remove dependent action");
    require(project.validate_integrity() == sm::result::success,
        "deletion cleanup left the project invalid");
}

} // namespace

int main() {
    try {
        character_scoped_references_are_rejected();
        character_root_is_transactional_and_semantic();
        missing_root_is_rejected_when_a_root_frame_is_required();
        structural_semantic_change_is_rejected_before_commit();
        deletion_cleanup_still_preserves_integrity();
        std::cout << "PASS animation character integrity\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
