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
    }

    sm::animation& anim() {
        return *project.core().animation_data(character).find_animation(animation);
    }
};

void keyframe_operations_and_frame_aware_undo() {
    fixture f;
    const auto original =
        f.project.core().topology().get<sm::node>(f.root)->get().world_pos();
    require(f.project.begin_animation_session(f.character, f.animation) == sm::result::success,
        "begin failed");
    require(f.project.add_animation_keyframe() == sm::result::success,
        "first keyframe failed");

    const auto first = f.project.animation_session_keyframe();
    require(first.has_value() && f.anim().keyframes.size() == 1,
        "first keyframe not selected");

    const sm::point pose_a{40, 50};
    f.project.transform_node_positions({{f.root, original}}, {{f.root, pose_a}});
    require(f.anim().find_keyframe(*first)->pose.root_positions.at(f.root) == pose_a,
        "pose A did not capture canvas edit");

    require(f.project.add_animation_keyframe() == sm::result::success, "append failed");
    const auto second = f.project.animation_session_keyframe();
    require(second.has_value() && *second != *first, "second keyframe identity invalid");
    require(f.anim().keyframes.size() == 2 && f.anim().transitions.size() == 1,
        "transition bookkeeping invalid");

    const sm::point pose_b{80, 90};
    f.project.transform_node_positions({{f.root, pose_a}}, {{f.root, pose_b}});
    require(f.project.select_animation_keyframe(*first) == sm::result::success,
        "select A failed");
    require(f.project.topology().get<sm::node>(f.root)->get().world_pos() == pose_a,
        "select did not display exact A pose");

    f.project.undo();
    require(f.project.animation_session_keyframe() == second,
        "undo did not reselect affected B keyframe");
    require(f.project.topology().get<sm::node>(f.root)->get().world_pos() == pose_a,
        "undo B edit did not restore B's previous pose");
    require(f.anim().find_keyframe(*first)->pose.root_positions.at(f.root) == pose_a,
        "undo B edit changed A");

    require(f.project.redo() == sm::result::success, "redo B edit failed");
    require(f.project.animation_session_keyframe() == second, "redo did not select B");
    require(f.project.topology().get<sm::node>(f.root)->get().world_pos() == pose_b,
        "redo B edit failed");

    const auto second_identity = *second;
    const auto transition_identity = f.anim().transitions.front().id;
    require(f.project.delete_animation_keyframe() == sm::result::success, "delete B failed");
    require(f.anim().keyframes.size() == 1, "delete count wrong");

    f.project.undo();
    require(f.anim().keyframes.size() == 2, "delete undo count wrong");
    require(f.anim().keyframes[1].id == second_identity,
        "delete undo changed keyframe identity");
    require(f.anim().transitions.front().id == transition_identity,
        "delete undo changed transition identity");

    require(f.project.redo() == sm::result::success && f.anim().keyframes.size() == 1,
        "delete redo failed");
    f.project.undo();
    f.project.end_animation_session();
    require(f.project.is_dirty(),
        "authored Animation Mode edits did not dirty document on session commit");
}

void transition_pin_ownership_and_undo() {
    fixture f;
    require(f.project.begin_animation_session(f.character, f.animation) == sm::result::success, "begin failed");
    require(f.project.add_animation_keyframe() == sm::result::success, "first key failed");
    const auto first = *f.project.animation_session_keyframe();
    require(f.project.set_animation_outgoing_transition_node_pinned(f.root, true) == sm::result::not_found,
        "terminal keyframe accepted hidden outgoing pin state");

    require(f.project.add_animation_keyframe() == sm::result::success, "second key failed");
    const auto second = *f.project.animation_session_keyframe();
    require(f.anim().transitions.size() == 1 && f.anim().transitions[0].pinned_nodes.empty(),
        "new first transition should start unpinned");

    require(f.project.select_animation_keyframe(first) == sm::result::success, "select first failed");
    require(f.project.set_animation_outgoing_transition_node_pinned(f.root, true) == sm::result::success, "pin failed");
    require(f.anim().transitions[0].pinned_nodes.contains(f.root), "pin was not stored on outgoing transition");
    require(f.project.animation_session_pinned_nodes().contains(f.root), "selected source did not expose outgoing pins");

    require(f.project.select_animation_keyframe(second) == sm::result::success, "select second failed");
    require(f.project.animation_session_pinned_nodes().empty(), "terminal keyframe reported outgoing pins");
    require(f.project.animation_session_incoming_locked_nodes().contains(f.root), "incoming transition pin not exposed as lock");
    require(f.project.animation_session_incoming_lock_source_label(f.root) == std::optional<std::string>{"Pose 1"},
        "incoming lock source did not identify preceding pose");

    require(f.project.add_animation_keyframe() == sm::result::success, "third key failed");
    const auto third = *f.project.animation_session_keyframe();
    require(f.anim().transitions.size() == 2 && f.anim().transitions[1].pinned_nodes.contains(f.root),
        "appended transition did not inherit current transition pin behavior");
    require(f.project.animation_session_pinned_nodes().empty(), "new terminal keyframe reported hidden pins");

    require(f.project.select_animation_keyframe(second) == sm::result::success, "reselect second failed");
    require(f.project.set_animation_outgoing_transition_node_pinned(f.root, false) == sm::result::success, "unpin failed");
    require(!f.anim().transitions[1].pinned_nodes.contains(f.root), "unpin did not affect second transition");
    require(f.anim().transitions[0].pinned_nodes.contains(f.root), "unpin leaked backward to first transition");

    f.project.undo();
    require(f.project.animation_session_keyframe() == second &&
        f.anim().transitions[1].pinned_nodes.contains(f.root), "pin undo did not restore transition state");
    require(f.project.redo() == sm::result::success &&
        !f.anim().transitions[1].pinned_nodes.contains(f.root), "pin redo failed");
    require(f.project.select_animation_keyframe(third) == sm::result::success, "select third failed");
    require(f.project.animation_session_pinned_nodes().empty(), "final keyframe did not remain pin-free");
    f.project.end_animation_session();
}

void persistence_validation_and_remap() {
    fixture f;
    require(f.project.begin_animation_session(f.character, f.animation) == sm::result::success,
        "begin failed");
    require(f.project.add_animation_keyframe() == sm::result::success, "first key failed");
    const auto first = *f.project.animation_session_keyframe();
    require(f.project.add_animation_keyframe() == sm::result::success, "second key failed");
    require(f.project.select_animation_keyframe(first) == sm::result::success, "select source failed");
    require(f.project.set_animation_outgoing_transition_node_pinned(f.root, true) == sm::result::success, "pin failed");
    require(f.project.rename_animation_keyframe(std::string("Contact")) == sm::result::success,
        "rename failed");

    const auto json = sm::animation_assets_to_json(f.project.core().animation_data(f.character));
    require(!json["animations"][0]["keyframes"][0].contains("pinned_nodes") &&
        !json["animations"][0]["keyframes"][1].contains("pinned_nodes"),
        "pins serialized on keyframes");
    require(json["animations"][0]["transitions"][0].contains("pinned_nodes") &&
        json["animations"][0]["transitions"][0]["pinned_nodes"].size() == 1,
        "transition pins missing from serialization");

    auto bytes = f.project.serialize();
    require(bytes.has_value(), "serialize failed");
    sm::project loaded;
    require(loaded.deserialize(*bytes) == sm::project_result::success, "round trip failed");

    const auto* animation = loaded.animation_data(f.character).find_animation(f.animation);
    require(animation && animation->keyframes.size() == 2 && animation->transitions.size() == 1,
        "animation did not round trip");
    require(animation->keyframes[0].id == first && animation->keyframes[0].name &&
        *animation->keyframes[0].name == "Contact",
        "keyframe identity/name did not round trip");
    require(animation->transitions[0].pinned_nodes.contains(f.root),
        "transition pins did not round trip");

    auto assets = f.project.core().animation_data(f.character);
    const auto new_root = sm::object_id::generate();
    sm::remap_animation_assets(assets, {{f.root, new_root}});
    auto* remapped = assets.find_animation(f.animation);
    require(remapped->keyframes[0].pose.root_positions.contains(new_root),
        "keyframe root reference was not remapped");
    require(remapped->transitions[0].pinned_nodes.contains(new_root) &&
        !remapped->transitions[0].pinned_nodes.contains(f.root), "transition pin was not remapped");

    f.project.end_animation_session();
}

void duplicate_delete_and_empty_sequence() {
    fixture f;
    require(f.project.begin_animation_session(f.character, f.animation) == sm::result::success,
        "begin failed");
    require(f.project.add_animation_keyframe() == sm::result::success, "first add failed");
    const auto first = *f.project.animation_session_keyframe();
    require(f.project.add_animation_keyframe() == sm::result::success, "second add failed");
    const auto second = *f.project.animation_session_keyframe();
    require(f.project.select_animation_keyframe(first) == sm::result::success, "select first failed");
    require(f.project.set_animation_outgoing_transition_node_pinned(f.root, true) == sm::result::success, "pin failed");
    const auto original_transition = f.anim().transitions[0].id;

    require(f.project.duplicate_animation_keyframe() == sm::result::success, "duplicate failed");
    require(f.anim().keyframes.size() == 3 && f.anim().keyframes[0].id == first &&
        f.anim().keyframes[1].id != first && f.anim().keyframes[2].id == second, "duplicate identity wrong");
    require(f.anim().transitions.size() == 2 &&
        f.anim().transitions[0].pinned_nodes.contains(f.root) &&
        f.anim().transitions[1].pinned_nodes.contains(f.root),
        "duplicating a pinned interval did not preserve pins on both halves");
    require(f.anim().transitions[1].id == original_transition,
        "duplicate did not retain original transition on the second half");

    require(f.project.delete_animation_keyframe() == sm::result::success,
        "delete duplicate failed");
    require(f.anim().keyframes.size() == 2 && f.anim().transitions.size() == 1 &&
        f.anim().transitions[0].id == original_transition &&
        f.anim().transitions[0].pinned_nodes.contains(f.root),
        "middle deletion did not preserve resulting transition pin semantics");

    require(f.project.select_animation_keyframe(second) == sm::result::success, "select final failed");
    require(f.project.delete_animation_keyframe() == sm::result::success, "delete final failed");
    require(f.project.delete_animation_keyframe() == sm::result::success, "delete last remaining failed");
    require(f.anim().keyframes.empty() && f.anim().transitions.empty() &&
        !f.project.animation_session_keyframe(),
        "final deletion did not produce empty sequence");

    f.project.end_animation_session();
}

void middle_delete_uses_incoming_pin_semantics() {
    fixture f;
    require(f.project.begin_animation_session(f.character, f.animation) == sm::result::success,
        "begin failed");
    require(f.project.add_animation_keyframe() == sm::result::success, "first add failed");
    const auto first = *f.project.animation_session_keyframe();
    require(f.project.add_animation_keyframe() == sm::result::success, "second add failed");
    const auto middle = *f.project.animation_session_keyframe();
    require(f.project.add_animation_keyframe() == sm::result::success, "third add failed");

    // Make the incoming and outgoing intervals observably different. Under the
    // previous keyframe-owned model, deleting the middle pose made its predecessor
    // the source of the resulting interval, so the predecessor's pins survived.
    require(f.project.select_animation_keyframe(first) == sm::result::success, "select first failed");
    require(f.project.set_animation_outgoing_transition_node_pinned(f.root, true) == sm::result::success,
        "pin incoming interval failed");
    require(f.anim().transitions[0].pinned_nodes.contains(f.root) &&
        !f.anim().transitions[1].pinned_nodes.contains(f.root),
        "test setup did not create distinct transition pin sets");
    const auto retained_outgoing_id = f.anim().transitions[1].id;
    const auto retained_outgoing_duration = f.anim().transitions[1].duration_seconds;

    require(f.project.select_animation_keyframe(middle) == sm::result::success, "select middle failed");
    require(f.project.delete_animation_keyframe() == sm::result::success, "delete middle failed");
    require(f.anim().keyframes.size() == 2 && f.anim().transitions.size() == 1,
        "middle delete did not reconcile sequence");
    require(f.anim().transitions[0].id == retained_outgoing_id &&
        f.anim().transitions[0].duration_seconds == retained_outgoing_duration,
        "middle delete did not retain outgoing transition metadata");
    require(f.anim().transitions[0].pinned_nodes.contains(f.root),
        "middle delete did not carry predecessor pin semantics onto resulting interval");
    f.project.end_animation_session();
}
}

int main() {
    try {
        keyframe_operations_and_frame_aware_undo();
        transition_pin_ownership_and_undo();
        persistence_validation_and_remap();
        duplicate_delete_and_empty_sequence();
        middle_delete_uses_incoming_pin_semantics();
        std::cout << "PASS Animation V2 Phase 2 core/model\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
