#include "core/sm_animation.hpp"
#include "core/sm_skeleton.hpp"
#include <iostream>
#include <stdexcept>
#include <algorithm>

void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template<class F> void rejects(F f, const char* message) {
    try { f(); } catch (const std::invalid_argument&) { return; }
    throw std::runtime_error(message);
}

int main() {
    try {
        sm::topology topology;
        auto a = sm::node_ref(topology.create_skeleton(sm::point{0, 0}).root_node());
        auto b = sm::node_ref(topology.create_skeleton(sm::point{10, 0}).root_node());
        auto bone = topology.create_bone("root", a, b);
        if (!bone) throw std::runtime_error("fixture failed");
        auto root = bone->get().id();
        auto skeleton = bone->get().owner().id();
        sm::animation_action consumer, producer;
        consumer.data = sm::rigid_translation{{skeleton}, sm::motion_path(sm::line_path{{0,0},{1,0}}), sm::translation_reference::character_root};
        producer.data = sm::rigid_translation{{skeleton}, sm::motion_path(sm::line_path{{0,0},{0,1}}), sm::translation_reference::animation_root};
        sm::animation animation;
        animation.layers = {{{consumer}}, {{producer}}};
        auto base = sm::capture_pose(topology, {skeleton}, "base");
        auto report = sm::evaluate_animation(animation, base, root, topology, 500);
        if (report.evaluation_order != std::vector<sm::object_id>{consumer.id, producer.id})
            throw std::runtime_error("evaluator silently reordered the visible stack");
        rejects([&] { sm::validate_animation_order(animation, root, topology); }, "invalid visible order accepted");
        const auto placed = sm::place_animation_action(animation, consumer.id, root, topology);
        require(sm::animation_evaluation_order(placed) == std::vector<sm::object_id>{producer.id, consumer.id}, "consumer not moved above producer");
        require(sm::animation_evaluation_order(animation) == std::vector<sm::object_id>{consumer.id, producer.id}, "placement mutated input");
        sm::validate_animation_order(placed, root, topology);
        auto unchanged = sm::place_animation_action(placed, consumer.id, root, topology);
        require(unchanged.layers.size() == placed.layers.size(), "valid placement changed layers");
        auto final = sm::evaluate_animation(placed, base, root, topology, 1000);
        auto frame = final.contexts.at(consumer.id).translation_reference_frame.value();
        require(sm::distance(frame.origin, {0,1}) < 1e-8, "adornment did not capture animated root frame");
        const auto direct = a->world_pos();
        sm::evaluate_animation(placed, base, root, topology, 250);
        sm::evaluate_animation(placed, base, root, topology, 1000);
        require(sm::distance(a->world_pos(), direct) < 1e-8, "evaluation depends on playback history");

        auto cyclic = animation;
        std::get<sm::rigid_translation>(cyclic.layers[1].actions[0].data).reference = sm::translation_reference::character_root;
        rejects([&] { sm::place_animation_action(cyclic, consumer.id, root, topology); }, "mutual references were accepted");
        rejects([&] { sm::place_animation_action(animation, producer.id, root, topology); }, "writer was moved downward past requested position");

        // Completed contributions still constrain later, nonoverlapping clips.
        auto late = consumer; late.start = 5000;
        sm::animation chronological; chronological.layers = {{{late, producer}}};
        sm::validate_animation_order(chronological, root, topology);
        require(sm::animation_evaluation_order(chronological) == std::vector<sm::object_id>{producer.id, consumer.id}, "same-layer order not chronological");
        auto late_producer = producer; late_producer.start = 5000;
        sm::animation wrong_time; wrong_time.layers = {{{consumer, late_producer}}};
        rejects([&] { sm::validate_animation_order(wrong_time, root, topology); }, "disjoint intervals exempted invalid order");
        sm::animation share; share.layers = {{{late}}, {{producer}}};
        auto shared = sm::place_animation_action(share, late.id, root, topology);
        require(shared.layers.size() == 2 && shared.layers[1].actions.size() == 2, "did not reuse compatible higher layer");

        // A pin protects a reference bone on the far side, but not its adjacent bone.
        auto c = sm::node_ref(topology.create_skeleton(sm::point{20,0}).root_node());
        auto d = sm::node_ref(topology.create_skeleton(sm::point{30,0}).root_node());
        auto bc = topology.create_bone("middle", b, c);
        require(bc.has_value(), "middle bone missing");
        auto cd = topology.create_bone("tip", c, d);
        require(cd.has_value(), "tip bone missing");
        sm::animation_action ik;
        sm::ik_translation translation;
        translation.effector = d->id(); translation.pins = {b->id()};
        translation.reference = sm::translation_reference::animation_root;
        ik.data = translation;
        auto scope = sm::animation_action_write_scope(ik, topology);
        require(std::ranges::find(scope, a->id()) == scope.end() && std::ranges::find(scope, b->id()) == scope.end(), "IK scope crossed pin");
        require(std::ranges::find(scope, c->id()) != scope.end() && std::ranges::find(scope, d->id()) != scope.end(), "IK scope omitted movable nodes");
        sm::animation protected_frame; protected_frame.layers = {{{consumer}}, {{ik}}};
        sm::validate_animation_order(protected_frame, root, topology);
        auto adjacent = consumer;
        auto& reference = std::get<sm::rigid_translation>(adjacent.data);
        reference.reference = sm::translation_reference::bone; reference.reference_bone = bc->get().id();
        protected_frame.layers[0].actions = {adjacent};
        rejects([&] { sm::validate_animation_order(protected_frame, root, topology); }, "bone adjoining pin incorrectly protected");
        auto ik_rotate = ik;
        ik_rotate.data = sm::ik_rotation{d->id(), b->id(), 1.0};
        require(sm::animation_action_write_scope(ik_rotate, topology) == scope, "IK rotation scope differs from pinned translation");

        sm::animation_action rotate;
        sm::rigid_rotation rigid; rigid.bone=root; rigid.pivot=sm::rotation_pivot::root;
        rotate.data=rigid;
        auto rotation_scope=sm::animation_action_write_scope(rotate,topology);
        require(std::ranges::find(rotation_scope,a->id())==rotation_scope.end(), "rotation pivot included in writes");
        require(std::ranges::find(rotation_scope,b->id())!=rotation_scope.end(), "rotation moving endpoint omitted");
        auto unrelated = producer;
        unrelated.id = sm::object_id::generate();
        auto other = topology.create_skeleton(sm::point{100,100}).id();
        std::get<sm::rigid_translation>(unrelated.data).skeletons = {other};
        sm::animation intermediate; intermediate.layers = {{{consumer}},{{producer}},{{unrelated}}};
        auto nearest = sm::place_animation_action(intermediate,consumer.id,root,topology);
        require(sm::animation_evaluation_order(nearest)==std::vector<sm::object_id>{producer.id,consumer.id,unrelated.id},
            "placement jumped past the nearest valid boundary");
        std::cout << "PASS animation_order\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
