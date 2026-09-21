#include "ui/stick_man.hpp"
#include "ui/panes/animation_pane.hpp"
#include "ui/panes/animation_timeline.hpp"
#include <QtWidgets>
#include <iostream>
#include <stdexcept>

void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
int main(int argc, char** argv) {
    QApplication app(argc, argv);
    try {
        ui::stick_man window;
        auto& project = window.project();
        project.add_new_skeleton_root({0,0});
        auto u = (*project.topology().skeletons().begin())->root_node().id();
        project.add_new_skeleton_root({10,0});
        sm::object_id v;
        for (auto skeleton : project.topology().skeletons()) if (skeleton->root_node().id() != u) v = skeleton->root_node().id();
        require(project.add_bone(u,v) == sm::result::success, "fixture bone failed");
        auto skeleton = project.topology().get<sm::node>(u)->get().owner().id();
        std::vector<sm::const_skel_ref> members{project.topology().get<sm::node>(u)->get().owner()};
        auto character = project.make_character(members).value();
        sm::animation_action producer, consumer;
        producer.data = sm::rigid_translation{{skeleton},sm::motion_path(sm::line_path{{0,0},{1,0}}),sm::translation_reference::animation_root};
        consumer.data = sm::rigid_translation{{skeleton},sm::motion_path(sm::line_path{{0,0},{0,1}}),sm::translation_reference::character_root};
        sm::animation animation;
        animation.base_pose = project.core().animation_data(character).default_pose;
        animation.layers = {{{producer}},{{consumer}}};
        project.edit_animation_data(character,[&](auto& data) { data.animations.push_back(animation); });
        auto* browser = window.findChild<ui::pane::animation*>();
        require(browser->open_animation(character,animation.id), "cannot open animation");
        auto* editor = window.findChild<ui::pane::animation_timeline*>();
        auto* timeline = editor->findChild<ui::timeline*>();
        auto current = [&]() -> const sm::animation& { return *project.core().animation_data(character).find_animation(animation.id); };
        const std::vector<sm::object_id> expected{producer.id,consumer.id};
        // Request an overlapping slot on the producer's layer. The editor must
        // find a higher placement rather than reject the requested overlap.
        timeline->itemMoveRequested(QString::fromStdString(consumer.id.to_string()),100,{ui::row_head_position::placement::on_row,1});
        require(sm::animation_evaluation_order(current())==expected, "UI did not preserve valid composition");
        require(current().layers[1].actions[0].start==100, "UI rejected automatic placement");
        project.undo();
        require(current().layers.size()==2 && current().layers[1].actions[0].start==0, "placement was not one undoable edit");
        // A writer cannot be moved above the action consuming its frame.
        timeline->itemMoveRequested(QString::fromStdString(producer.id.to_string()),0,{ui::row_head_position::placement::between_rows,0});
        require(current().layers.size()==2 && sm::animation_evaluation_order(current())==expected, "invalid move changed animation");
        std::cout << "PASS animation_placement_ui\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
