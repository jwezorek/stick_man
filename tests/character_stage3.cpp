#include "model/project.hpp"
#include "model/selection.hpp"
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
void selection_and_translation() {
    mdl::project model;
    auto& core = model.core();
    auto& body = core.create_skeleton({0, 0});
    auto& end = core.create_skeleton({40, 0});
    require(core.create_bone("arm", body.root_node(), end.root_node()).has_value(), "fixture bone");
    auto& eye = core.create_skeleton({70, 30});
    auto& loose = core.create_skeleton({200, 0});
    std::vector<sm::const_skel_ref> rig{body, eye};
    auto id = model.make_character(rig);
    require(id.has_value(), "make character");
    mdl::selection selected{core.character(*id).value()};
    require(std::holds_alternative<sm::const_character_ref>(selected.front()), "character selection must be distinct");
    auto topology = mdl::selection_topology(selected);
    require(topology.size() == 4, "character selection must resolve all rig nodes and bones");
    auto inferred = mdl::infer_selection(topology);
    require(inferred.size() == 1 && std::holds_alternative<sm::const_character_ref>(inferred.front()), "complete topology must infer character");
    mdl::selection explicit_child{sm::const_skel_ref(body)};
    auto incomplete = mdl::infer_selection(explicit_child);
    require(incomplete.size() == 1 && std::holds_alternative<sm::const_skel_ref>(incomplete.front()), "incomplete rig must stay skeleton");
    auto partial = mdl::infer_selection({sm::const_node_ref(body.root_node()), sm::const_skel_ref(eye)});
    require(partial.size() == 2 && std::holds_alternative<sm::const_node_ref>(partial.front()), "partial component must remain topology");
    topology.push_back(sm::const_skel_ref(loose));
    auto mixed = mdl::infer_selection(topology);
    require(mixed.size() == 3 && std::ranges::all_of(mixed, [](auto object) {return std::holds_alternative<sm::const_skel_ref>(object);}), "rig plus loose must remain skeletons");
    std::vector<mdl::handle> nodes;
    for (auto object : mdl::selection_topology(selected)) if (auto node = std::get_if<sm::const_node_ref>(&object)) nodes.push_back(node->get().id());
    model.transform(nodes, std::function<void(sm::node&)>([](sm::node& node) { node.set_world_pos(node.world_pos() + sm::point{10, 20}); }));
    require(sm::distance(body.root_node().world_pos(), {10, 20}) < .001 && sm::distance(eye.root_node().world_pos(), {80, 50}) < .001, "complete rig translation");
    model.undo();
    require(sm::distance(eye.root_node().world_pos(), {70, 30}) < .001, "translation undo");
    model.redo();
    require(sm::distance(eye.root_node().world_pos(), {80, 50}) < .001, "translation redo");
}
void authoring_history() {
    mdl::project model;
    auto& core = model.core();
    auto& body = core.create_skeleton({0, 0});
    auto& eye = core.create_skeleton({70, 30});
    auto body_id = body.id(), eye_id = eye.id();
    std::vector<sm::const_skel_ref> rig{body};
    require(!model.make_character({}) && !model.can_undo(), "empty creation must not enter history");
    auto id = model.make_character(rig);
    require(id.has_value(), "make one-component character");
    mdl::selection explicit_child{sm::const_skel_ref(body)};
    require(std::holds_alternative<sm::const_skel_ref>(explicit_child.front()), "explicit pane selection is not inferred");
    require(std::holds_alternative<sm::const_character_ref>(mdl::infer_selection(explicit_child).front()), "canvas one-component inference");
    require(!model.make_character(rig), "already-owned creation must reject");
    model.undo(); require(!core.character(*id) && body.is_loose(), "creation undo");
    model.redo(); require(core.character(*id).has_value(), "creation redo identity");
    model.rename(*id, "Alice");
    model.undo(); require(core.character(*id)->get().name() != "Alice", "rename undo");
    model.redo(); require(core.character(*id)->get().name() == "Alice", "rename redo");
    std::vector<sm::const_skel_ref> adopt{eye};
    require(model.adopt_skeletons(*id, adopt) == sm::result::success, "adopt");
    model.undo(); require(eye.is_loose(), "adoption undo");
    model.redo(); require(core.character(*id)->get().rig().size() == 2, "adoption redo");
    require(model.replace_skeletons({eye_id}, {}) == sm::result::success, "component delete");
    require(core.character(*id)->get().rig().size() == 1, "nonfinal retains character");
    require(model.delete_character(*id) == sm::result::success && !core.character(*id), "character semantic delete");
    model.undo();
    require(core.character(*id)->get().rig().contains(body_id), "final-component undo restores identity");
    model.undo(); require(core.character(*id)->get().rig().contains(eye_id), "component undo restores rig");
    require(core.has_consistent_membership(), "consistent membership after history");
}
}
int main() {
    try { selection_and_translation(); authoring_history(); std::cout << "PASS character stage 3 model\n"; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
