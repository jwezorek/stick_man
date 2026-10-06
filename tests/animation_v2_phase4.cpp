#include "ui/animation_playback.hpp"
#include "ui/widgets/pose_strip_layout.hpp"
#include "model/project.hpp"
#include "core/sm_animation.hpp"
#include <QCoreApplication>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {
    void require(bool ok, const char* message) {
        if (!ok)
            throw std::runtime_error(message);
    }
void near(double a, double b, double eps = 1e-9) {
    if (std::abs(a - b) > eps)
        throw std::runtime_error("numeric mismatch");
}

void seek_contract() {
    ui::animation_playback playback;
    playback.set_duration(2.0);
    require(playback.seek(1.25), "finite seek rejected");
    near(playback.time(), 1.25);
    require(!playback.playing(), "seek did not pause");
    const double before = playback.time();
    require(!playback.seek(std::numeric_limits<double>::infinity()), "nonfinite seek accepted");
    near(playback.time(), before);
    require(playback.seek(9.0), "clamped seek rejected");
    near(playback.time(), 2.0);
    playback.play();
    require(playback.playing(), "play-at-end did not restart");
    near(playback.time(), 0.0);
    require(playback.seek(0.75), "seek while playing rejected");
    require(!playback.playing(), "seek while playing did not pause");
    near(playback.time(), 0.75);
}

void strip_mapping() {
    sm::animation a;
    a.keyframes.resize(3);
    a.reconcile_transitions();
    a.transitions[0].duration_seconds = 0.01; // force minimum pixel width
    a.transitions[1].duration_seconds = 2.0;
    ui::pose_strip_layout layout(&a);
    require(layout.cards.size() == 3 && layout.transitions.size() == 2, "layout shape");
    near(*layout.time_at_x(layout.cards[0].rect.center().x()), 0.0);
    near(*layout.time_at_x(layout.cards[1].rect.center().x()), 0.01);
    near(*layout.time_at_x(layout.transitions[0].rect.center().x()), 0.005);
    near(*layout.time_at_x(layout.transitions[1].rect.center().x()), 1.01);
    auto p = layout.at_time(0.005);
    require(p && p->x > layout.transitions[0].rect.left() && p->x < layout.transitions[0].rect.right(),
        "time-to-position did not use transition span");
}

struct fixture {
    mdl::project project;
    sm::object_id character;
    sm::object_id animation_id;
    sm::object_id root;
    sm::object_id tip;
    sm::object_id bone;

    fixture() {
        auto& core = project.core();
        sm::node_ref r = core.create_skeleton({0, 0}).root_node();
        sm::node_ref t = core.create_skeleton({10, 0}).root_node();
        auto created_bone = core.create_bone("bone", r, t);
        require(created_bone.has_value(), "create bone");
        bone = created_bone->get().id();
        root = r->id();
        tip = t->id();
        character = project.make_character(std::vector<sm::const_skel_ref>{r->owner()}).value();
        auto rig = core.character(character)->get().rig().skeleton_ids();
        sm::animation a;
        a.name = "phase4";
        a.keyframes.resize(2);
        for (auto& k : a.keyframes)
            k.pose = sm::capture_skeletal_pose(core.topology(), rig);
        a.reconcile_transitions();
        a.transitions[0].duration_seconds = 2.0;
        animation_id = a.id;
        project.edit_animation_data(character, [&](auto& assets) { assets.animations.push_back(a); });
        project.mark_saved();
        require(project.begin_animation_session(character, animation_id) == sm::result::success, "begin session");
    }
};

void duration_and_insert() {
    fixture f;
    auto* a = f.project.core().animation_data(f.character).find_animation(f.animation_id);
    const auto transition_id = a->transitions[0].id;
    require(f.project.set_animation_outgoing_transition_node_pinned(f.tip, true) == sm::result::success,
        "pin before split failed");
    require(f.project.set_animation_transition_duration(transition_id, 4.0) == sm::result::success,
        "duration edit failed");
    near(a->transitions[0].duration_seconds, 4.0);
    require(f.project.can_undo(), "duration edit missing history");
    f.project.undo();
    near(a->transitions[0].duration_seconds, 2.0);
    require(f.project.redo() == sm::result::success, "duration redo failed");
    near(a->transitions[0].duration_seconds, 4.0);
    const auto original_id = a->transitions[0].id;
    require(f.project.insert_animation_keyframe(1.5) == sm::result::success, "insert failed");
    require(a->keyframes.size() == 3 && a->transitions.size() == 2, "insert shape");
    require(a->transitions[0].id == original_id, "first split did not preserve transition id");
    require(a->transitions[1].id != original_id, "second split did not get fresh id");
    require(a->transitions[0].pinned_nodes.contains(f.tip) &&
        a->transitions[1].pinned_nodes.contains(f.tip), "split did not preserve pins on both halves");
    near(a->transitions[0].duration_seconds, 1.5);
    near(a->transitions[1].duration_seconds, 2.5);
    near(a->duration_seconds(), 4.0);
    require(f.project.animation_session_keyframe() == a->keyframes[1].id, "inserted pose not selected");
    const auto inserted_id = a->keyframes[1].id;
    const auto second_transition_id = a->transitions[1].id;
    f.project.undo();
    require(a->keyframes.size() == 2 && a->transitions.size() == 1, "insert undo failed");
    require(f.project.redo() == sm::result::success, "insert redo failed");
    require(a->keyframes[1].id == inserted_id && a->transitions[1].id == second_transition_id,
        "redo regenerated stable identities");
}

void transition_rotation_authoring_and_structure() {
    fixture f;
    auto* a = f.project.core().animation_data(f.character).find_animation(f.animation_id);
    const auto first_key = a->keyframes[0].id;
    auto added = f.project.add_animation_rotation_constraint(
        f.bone, sm::rotation_reference::world(), {-0.25, 0.5});
    require(added.has_value(), "transition rotation constraint create failed");
    const auto cid = *added;
    require(a->transitions[0].rotation_constraints.contains(cid), "constraint not owned by transition");
    require(!f.project.core().constraints().contains(cid), "transition constraint leaked into persistent constraints");

    require(f.project.update_animation_rotation_constraint(cid,
        sm::rotation_constraint{f.bone, sm::rotation_reference::world(), {0.1, 0.2}}) == sm::result::success,
        "transition constraint update failed");
    near(a->transitions[0].rotation_constraints.at(cid).rotation()->allowed.start_angle, 0.1);
    f.project.undo();
    near(a->transitions[0].rotation_constraints.at(cid).rotation()->allowed.start_angle, -0.25);
    require(f.project.redo() == sm::result::success, "transition constraint redo failed");
    near(a->transitions[0].rotation_constraints.at(cid).rotation()->allowed.start_angle, 0.1);

    const auto original_transition_id = a->transitions[0].id;
    require(f.project.insert_animation_keyframe(1.0) == sm::result::success,
        "split constrained transition failed");
    require(a->transitions[0].id == original_transition_id, "split changed first transition ID");
    require(a->transitions[0].rotation_constraints.contains(cid),
        "split did not preserve original constraint on first half");
    require(a->transitions[1].rotation_constraints.size() == 1,
        "split did not preserve transition constraint on second half");
    const auto second_cid = a->transitions[1].rotation_constraints.begin()->first;
    require(second_cid != cid, "split reused transition-local constraint identity");
    const auto* first_rotation = a->transitions[0].rotation_constraints.at(cid).rotation();
    const auto* second_rotation = a->transitions[1].rotation_constraints.begin()->second.rotation();
    require(first_rotation && second_rotation && first_rotation->target_bone == second_rotation->target_bone &&
        first_rotation->reference == second_rotation->reference &&
        first_rotation->allowed.start_angle == second_rotation->allowed.start_angle &&
        first_rotation->allowed.span_angle == second_rotation->allowed.span_angle,
        "split changed transition constraint semantics");

    require(f.project.select_animation_keyframe(a->keyframes[1].id) == sm::result::success,
        "select inserted key failed");
    require(f.project.duplicate_animation_keyframe() == sm::result::success,
        "duplicate constrained key failed");
    require(a->transitions.size() == 3, "duplicate transition count wrong");
    require(a->transitions[1].rotation_constraints.size() == 1 &&
        a->transitions[2].rotation_constraints.size() == 1,
        "duplicate lost transition constraint semantics");

    require(f.project.select_animation_keyframe(a->keyframes[1].id) == sm::result::success,
        "reselect key for delete failed");
    require(f.project.delete_animation_keyframe() == sm::result::success,
        "delete constrained key failed");
    require(a->transitions.size() == 2 && a->transitions[0].rotation_constraints.contains(cid),
        "delete lost resulting transition constraint");

    require(f.project.select_animation_keyframe(a->keyframes.back().id) == sm::result::success,
        "select terminal failed");
    require(!f.project.add_animation_rotation_constraint(
        f.bone, sm::rotation_reference::world(), {0, 1}).has_value(),
        "terminal keyframe accepted hidden transition constraint");

    require(f.project.select_animation_keyframe(first_key) == sm::result::success,
        "reselect first key failed");
    require(f.project.remove_animation_rotation_constraint(cid) == sm::result::success,
        "transition constraint removal failed");
    require(!a->transitions[0].rotation_constraints.contains(cid), "constraint removal did not apply");
    f.project.undo();
    require(a->transitions[0].rotation_constraints.contains(cid), "constraint removal undo failed");
}

void pin_endpoint_invariant() {
    fixture f;
    auto* a = f.project.core().animation_data(f.character).find_animation(f.animation_id);
    require(f.project.set_animation_outgoing_transition_node_pinned(f.tip, true) == sm::result::success,
        "matching endpoint pin rejected");
    require(f.project.select_animation_keyframe(a->keyframes[1].id) == sm::result::success, "select destination");
    require(f.project.animation_session_incoming_locked_nodes().contains(f.tip), "incoming lock not derived");

    auto old_root = f.project.topology().get<sm::node>(f.root)->get().world_pos();
    auto old_tip = f.project.topology().get<sm::node>(f.tip)->get().world_pos();
    const bool dirty_before = f.project.is_dirty();
    const bool undo_before = f.project.can_undo();
    f.project.transform_node_positions({ { f.root, old_root }, { f.tip, old_tip } },
        { { f.root, old_root + sm::point{ 1, 0 } }, { f.tip, old_tip + sm::point{ 1, 0 } } });
    require(f.project.topology().get<sm::node>(f.tip)->get().world_pos() == old_tip,
        "incoming lock movement was committed");
    require(f.project.is_dirty() == dirty_before && f.project.can_undo() == undo_before,
        "rejected locked edit changed history");

    require(f.project.select_animation_keyframe(a->keyframes[0].id) == sm::result::success, "select source");
    require(f.project.set_animation_outgoing_transition_node_pinned(f.tip, false) == sm::result::success, "unpin failed");
    a->keyframes[1].pose.root_positions[f.root] = {1, 0};
    require(f.project.set_animation_outgoing_transition_node_pinned(f.tip, true) == sm::result::invalid_animation,
        "mismatched endpoint pin accepted");
    require(!a->transitions[0].pinned_nodes.contains(f.tip), "failed pin mutated transition state");

    // Directly injected inconsistent transition data is not a valid continuous preview,
    // but the authoring API still permits removing the offending pin.
    a->transitions[0].pinned_nodes.insert(f.tip);
    auto rig = f.project.core().character(f.character)->get().rig().skeleton_ids();
    auto sample = sm::sample_constrained_pose(*a, 1.0, f.project.core().topology(), rig);
    require(!sample && sample.error() == sm::result::invalid_animation,
        "legacy mismatched interval preview was accepted");
    require(f.project.set_animation_outgoing_transition_node_pinned(f.tip, false) == sm::result::success,
        "legacy offending pin could not be removed");
}
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    try {
        seek_contract();
        strip_mapping();
        duration_and_insert();
        transition_rotation_authoring_and_structure();
        pin_endpoint_invariant();
        std::cout << "PASS Animation V2 Phase 4\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
