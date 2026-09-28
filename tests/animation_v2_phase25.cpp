#include "ui/animation_playback.hpp"
#include "ui/widgets/pose_strip_layout.hpp"
#include "model/project.hpp"
#include "json.hpp"
#include <QCoreApplication>
#include <QThread>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void near(double actual, double expected, const char* message) {
    require(std::abs(actual - expected) < 1e-9, message);
}

void geometry_skips_instantaneous_cards() {
    sm::animation animation;
    animation.keyframes.resize(3);
    animation.reconcile_transitions();
    animation.transitions[0].duration_seconds = 0.4;
    animation.transitions[1].duration_seconds = 1.2;
    ui::pose_strip_layout layout(&animation);
    near(animation.duration_seconds(), 1.6, "cards added fake animation time");
    near(layout.transitions[1].rect.width() / layout.transitions[0].rect.width(), 3,
        "transition widths do not represent durations");
    for (const auto& test : {std::pair{0.0, std::size_t{0}}, {0.2, 0}, {0.4, 1}, {1.0, 1}, {1.6, 2}}) {
        const auto position = layout.at_time(test.first);
        require(position && position->current_pose == test.second, "wrong current pose at animation time");
    }
    near(layout.at_time(0)->x, layout.cards[0].rect.right(), "start sweeps through first card");
    near(layout.at_time(0.2)->x, layout.transitions[0].rect.center().x(), "first transition midpoint wrong");
    require(layout.at_time(0.4)->at_keyframe && !layout.at_time(0.2)->at_keyframe,
        "keyframe instant not distinguished from transition");
    near(layout.at_time(0.4)->x, layout.cards[1].rect.right(), "keyframe boundary failed to jump card");
    near(layout.at_time(1.0)->x, layout.transitions[1].rect.center().x(), "second transition midpoint wrong");
    near(layout.at_time(1.6)->x, layout.cards[2].rect.right(), "final playhead not at end");
    near(layout.at_time(100)->x, layout.cards[2].rect.right(), "final time not clamped");
    ui::pose_strip_layout wider(&animation, 300);
    near(wider.cards.back().time, 1.6, "changing card width changed time");
    near(wider.at_time(1.0)->x - layout.at_time(1.0)->x, 300,
        "time mapping failed to account for zero-time cards");

    animation.transitions[0].duration_seconds = 0.001;
    ui::pose_strip_layout short_transition(&animation);
    require(short_transition.transitions[0].rect.width() >= 10, "short transition became invisible");
    near(short_transition.at_time(0.0005)->x, short_transition.transitions[0].rect.center().x(),
        "minimum visual width changed transition time");
    require(!short_transition.same_timing(layout), "duration edit not detected");
    animation.keyframes.clear();
    animation.reconcile_transitions();
    require(!ui::pose_strip_layout(&animation).at_time(0), "empty animation has current pose");
    animation.keyframes.resize(1);
    require(ui::pose_strip_layout(&animation).at_time(100)->current_pose == 0,
        "single-pose animation failed");
    near(animation.duration_seconds(), 0, "single pose has duration");
    animation.keyframes.resize(3);
    animation.reconcile_transitions();
    animation.transitions[0].duration_seconds = 1e200;
    animation.transitions[1].duration_seconds = 2e200;
    const ui::pose_strip_layout long_clip(&animation);
    require(std::isfinite(long_clip.width) && long_clip.width <= 16777215,
        "long animation geometry exceeds Qt widget limits");
    near(long_clip.transitions[1].rect.width() / long_clip.transitions[0].rect.width(), 2,
        "long animation lost proportional timing widths");
}

void names_transitions_and_sequence_edits() {
    mdl::project model;
    auto& skeleton = model.core().create_skeleton({0, 0});
    const auto character = model.core().create_character(std::vector<sm::const_skel_ref>{skeleton}).value()->id();
    sm::animation source;
    const auto id = source.id;
    model.core().animation_data(character).animations.push_back(source);
    require(model.begin_animation_session(character, id) == sm::result::success, "begin failed");
    auto animation = [&]() -> sm::animation& {
        return *model.core().animation_data(character).find_animation(id);
    };
    for (int i = 0; i < 3; ++i) require(model.add_animation_keyframe() == sm::result::success, "add failed");
    const auto first = animation().keyframes[0].id;
    const auto last = animation().keyframes[2].id;
    model.select_animation_keyframe(first);
    model.rename_animation_keyframe(std::string("Standing"));
    require(animation().keyframes[0].id == first, "rename changed identity");
    model.select_animation_keyframe(animation().keyframes[1].id);
    model.rename_animation_keyframe(std::string("Standing")); // Duplicate names are permitted.
    animation().transitions[0].duration_seconds = 0.35;
    animation().transitions[1].duration_seconds = 1.2;
    const auto transition0 = animation().transitions[0].id;
    const auto transition1 = animation().transitions[1].id;

    auto json = sm::animation_assets_to_json(model.core().animation_data(character));
    require(!json["animations"][0]["keyframes"][2].contains("name"), "generated fallback persisted as a name");
    const auto loaded = sm::animation_assets_from_json(json);
    const auto* roundtrip = loaded.find_animation(id);
    require(roundtrip->keyframes[0].name == "Standing" && roundtrip->keyframes[1].name == "Standing" &&
        !roundtrip->keyframes[2].name && roundtrip->keyframes[2].id == last, "names or identities failed round trip");
    near(roundtrip->transitions[0].duration_seconds, 0.35, "first duration failed round trip");
    near(roundtrip->transitions[1].duration_seconds, 1.2, "second duration failed round trip");
    near(roundtrip->duration_seconds(), 1.55, "total duration not transition sum");
    auto overflowing = loaded;
    for (auto& transition : overflowing.find_animation(id)->transitions) {
        transition.duration_seconds = std::numeric_limits<double>::max();
    }
    bool rejected = false;
    try { overflowing.validate(); }
    catch (const std::invalid_argument&) { rejected = true; }
    require(rejected, "overflowing total duration was accepted");
    require(ui::pose_keyframe_label(animation(), 2) == "Pose 3", "unnamed label wrong");

    model.select_animation_keyframe(first);
    model.duplicate_animation_keyframe();
    require(animation().transitions[1].id == transition0 && animation().transitions[2].id == transition1,
        "insertion detached existing transitions from their destination poses");
    near(animation().transitions[1].duration_seconds, 0.35, "insertion changed existing duration");
    require(ui::pose_keyframe_label(animation(), 3) == "Pose 4", "fallback did not change after insertion");
    model.delete_animation_keyframe();
    require(animation().transitions[0].id == transition0 && animation().transitions[1].id == transition1,
        "deletion lost the surviving transition associations");
    model.undo();
    require(animation().transitions[1].id == transition0, "undo did not restore transitions");
    model.redo();
    const ui::pose_strip_layout before(&animation());
    // No reorder UI exists yet: Core ordering is authoritative, with positional transitions.
    std::swap(animation().keyframes[0], animation().keyframes[2]);
    const ui::pose_strip_layout reordered(&animation());
    require(!before.same_timing(reordered), "reorder failed to invalidate playback");
    require(ui::pose_keyframe_label(animation(), 0) == "Pose 1" && !animation().keyframes[0].name,
        "fallback did not follow reordered keyframe position");
    require(reordered.cards[0].id == last && reordered.transitions[0].id == transition0 &&
        reordered.transitions[1].id == transition1, "geometry did not follow Core transition ordering");
    near(reordered.transitions[0].duration, 0.35, "reorder changed transition duration");
    model.end_animation_session();
}

void clock_uses_elapsed_time_and_stops_cleanly(QCoreApplication& app) {
    ui::animation_playback clock;
    clock.play();
    require(!clock.playing(), "empty clock played");
    clock.set_duration(2);
    clock.play();
    QThread::msleep(90); // Deliberately no paints/events during the stall.
    app.processEvents();
    require(clock.time() >= 0.08 && clock.time() < 1, "clock counted ticks instead of elapsed time");
    clock.pause();
    const double paused = clock.time();
    QThread::msleep(40);
    app.processEvents();
    near(clock.time(), paused, "pause advanced time");
    clock.play();
    QThread::msleep(50);
    clock.pause();
    require(clock.time() >= paused + 0.04, "resume lost elapsed time");
    clock.stop();
    near(clock.time(), 0, "stop failed to reset");
    require(!clock.playing(), "stop left timer active");
    clock.set_duration(0.03);
    clock.play();
    QThread::msleep(60);
    app.processEvents();
    near(clock.time(), 0.03, "final time was not exact");
    require(!clock.playing(), "final pose did not stop playback");
    clock.play();
    require(clock.playing() && clock.time() == 0, "Play at end did not restart");
    clock.set_duration(0.5);
    require(!clock.playing() && clock.time() == 0, "timing edit did not reset playback");
    clock.go_to_end();
    near(clock.time(), 0.5, "end transport failed");
}
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    try {
        geometry_skips_instantaneous_cards();
        names_transitions_and_sequence_edits();
        clock_uses_elapsed_time_and_stops_cleanly(app);
        std::cout << "PASS Animation V2 Phase 2.5 geometry/model/clock\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
