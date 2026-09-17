#include "core/sm_project.hpp"
#include <cmath>
#include <iostream>
#include <numbers>
#include <stdexcept>

void require(bool ok, const char* msg) { if (!ok) throw std::runtime_error(msg); }
void near(sm::point p, double x, double y) { require(std::abs(p.x-x)<1e-7 && std::abs(p.y-y)<1e-7,"unexpected evaluated position"); }
int main() {
    try {
        sm::project p;
        auto& root = p.create_skeleton({0,0}).root_node();
        auto& tip = p.create_skeleton({10,0}).root_node();
        auto& bone = p.create_bone("arm",root,tip).value().get();
        std::vector<sm::const_skel_ref> rig{root.owner()};
        auto c = p.create_character(rig).value();
        const auto base = c->animation_data().poses.front();
        sm::topology working; require(bool(root.owner().copy_to(working)),"copy rig");
        sm::animation a; a.base_pose = base.id;
        a.layers = {{{{sm::object_id::generate(),100,1000,sm::easing::linear,
            sm::rigid_rotation{bone.id(),sm::rotation_pivot::root,std::numbers::pi/2}}}}};
        require(sm::evaluate_animation(a,base,working,600).invalid_actions.empty(),"valid rotation");
        near(working.get<sm::node>(tip.id())->get().world_pos(),std::sqrt(50),std::sqrt(50));
        sm::evaluate_animation(a,base,working,5000);
        near(working.get<sm::node>(tip.id())->get().world_pos(),0,10);
        sm::evaluate_animation(a,base,working,0);
        near(working.get<sm::node>(tip.id())->get().world_pos(),10,0);
        sm::evaluate_animation(a,base,working,1100);
        near(working.get<sm::node>(tip.id())->get().world_pos(),0,10);
        near(tip.world_pos(),10,0);
        std::get<sm::rigid_rotation>(a.layers[0].actions[0].data).pivot = sm::rotation_pivot::tip;
        sm::evaluate_animation(a,base,working,1100);
        near(working.get<sm::node>(root.id())->get().world_pos(),10,-10);
        near(working.get<sm::node>(tip.id())->get().world_pos(),10,0);
        auto next = a.layers[0].actions[0]; next.id = sm::object_id::generate(); next.start = 1100;
        a.layers[0].actions.insert(a.layers[0].actions.begin(),next); // Intentionally unsorted.
        sm::evaluate_animation(a,base,working,2100);
        near(working.get<sm::node>(root.id())->get().world_pos(),20,0);
        std::get<sm::rigid_rotation>(a.layers[0].actions[0].data).bone = sm::object_id::generate();
        auto report = sm::evaluate_animation(a,base,working,2100);
        require(report.invalid_actions.size()==1,"missing target is reported");
        near(working.get<sm::node>(root.id())->get().world_pos(),10,-10);
        std::cout << "rotation evaluation passed\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
