#include "model/project.hpp"
#include "core/sm_constraint.hpp"
#include <cmath>
#include <iostream>
#include <numbers>
#include <stdexcept>

namespace {
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
bool near(sm::point a, sm::point b) { return sm::distance(a,b) < 1e-7; }
sm::bone& bone(mdl::project& p, sm::object_id id) { return p.topology().get<sm::bone>(id)->get(); }
sm::object_id arm(mdl::project& p, sm::object_id root, sm::point tip) {
    const auto end = p.core().create_skeleton(tip).root_node().id();
    return p.core().create_bone("arm", p.topology().get<sm::node>(root)->get(),
        p.topology().get<sm::node>(end)->get()).value()->id();
}
void editor_history() {
    mdl::project p;
    const auto root = p.core().create_skeleton({0,0}).root_node().id();
    const auto a = arm(p,root,{100,0}), b = arm(p,root,{0,100});
    const auto arbitrary = p.core().add_rotation_constraint(a, sm::rotation_reference::bone(b),
        {-std::numbers::pi,2*std::numbers::pi}, "Reference relation").value()->id();

    const auto created = p.add_rotation_constraint(a, sm::rotation_reference::world(), {-1,2},
        "Editor limit").value();
    require(p.core().constraints().size()==2,"model creation leaves arbitrary relation intact");
    require(p.core().constraint_by_id(created)->get().rotation()->allowed.start_angle==-1,
        "model creation stores requested range");
    p.undo(); require(p.core().constraints().size()==1,"undo creation retains arbitrary relation");
    p.redo(); require(p.core().constraint_by_id(created).has_value(),"creation redo retains ID");

    p.rename(created,"Named editor limit");
    auto edited = p.core().constraint_by_id(created)->get().definition();
    auto& rotation = std::get<sm::rotation_constraint>(edited);
    rotation.allowed = {-0.5,1};
    require(p.update_constraint(created, edited)==sm::result::success,"model edit failed");
    require(p.core().constraint_by_id(created)->get().name()=="Named editor limit","edit retains name");
    p.undo(); require(p.core().constraint_by_id(created)->get().rotation()->allowed.start_angle==-1,
        "undo restores definition");
    p.redo(); require(p.core().constraint_by_id(created)->get().rotation()->allowed.start_angle==-0.5,
        "redo restores definition");

    require(p.remove_constraint(created)==sm::result::success,"model removal failed");
    require(p.core().constraints().size()==1 && p.core().constraint_by_id(arbitrary).has_value(),
        "remove targets only selected first-class relation");
    p.undo(); require(p.core().constraint_by_id(created).has_value(),"undo removal retains ID");
    p.redo(); require(!p.core().constraint_by_id(created),"redo removes same ID");

    p.undo(); // restore the rotation relation before exercising triangle commands
    const auto triangle = p.add_rigid_triangle_constraint(a,b,"Rigid pair").value();
    auto triangle_def = p.core().constraint_by_id(triangle)->get().definition();
    std::get<sm::rigid_triangle_constraint>(triangle_def).relative_angle = 0.75;
    require(p.update_constraint(triangle,triangle_def)==sm::result::success,"triangle angle edit failed");
    require(std::abs(p.core().constraint_by_id(triangle)->get().triangle()->relative_angle-0.75)<1e-12,
        "triangle angle edit not stored");
    p.undo();
    require(std::abs(p.core().constraint_by_id(triangle)->get().triangle()->relative_angle-
        std::numbers::pi/2)<1e-12,"triangle angle undo failed");
    p.undo(); require(!p.core().constraint_by_id(triangle),"triangle create undo failed");
    p.redo(); require(p.core().constraint_by_id(triangle).has_value(),"triangle create redo changed identity");
}

void fan_history_and_copy() {
    mdl::project p;
    const auto root = p.core().create_skeleton({0,0}).root_node().id();
    const auto a = arm(p,root,{100,0}), b = arm(p,root,{0,100});
    const auto triangle = p.core().add_rigid_triangle_constraint(a,b).value()->id();
    const auto arbitrary = p.core().add_rotation_constraint(a,sm::rotation_reference::bone(b),
        {-std::numbers::pi,2*std::numbers::pi}).value()->id();
    p.transform(std::vector<mdl::handle>{a}, [](sm::bone& target) { target.rotate_by(std::numbers::pi/2,{},true); });
    require(near(bone(p,a).child_node().world_pos(),{0,100}) && near(bone(p,b).child_node().world_pos(),{-100,0}),"single-bone transform moves fan");
    p.undo(); require(near(bone(p,a).child_node().world_pos(),{100,0}) && near(bone(p,b).child_node().world_pos(),{0,100}),"undo restores fan atomically");
    p.redo(); require(near(bone(p,b).child_node().world_pos(),{-100,0}),"redo restores fan");
    require(p.core().constraint_by_id(triangle).has_value() && p.core().constraint_by_id(arbitrary).has_value(),"transform retains relation identities");
    const auto tip = bone(p,a).child_node().id();
    p.transform(std::vector<mdl::handle>{tip}, [](sm::node& target) { target.set_world_pos({-100,0}); });
    require(near(bone(p,b).child_node().world_pos(),{0,-100}),"node transform rotates rigid sibling");
    p.undo(); require(near(bone(p,b).child_node().world_pos(),{-100,0}),"node undo restores sibling snapshot");
    p.redo(); require(near(bone(p,b).child_node().world_pos(),{0,-100}),"node redo restores sibling snapshot");
    sm::topology clipboard;
    require(bone(p,a).owner().copy_to(clipboard).has_value(),"copy semantic topology");
    std::vector<sm::skel_ref> replacements;
    for (auto skeleton : clipboard.skeletons()) replacements.push_back(skeleton);
    require(p.replace_skeletons({},replacements)==sm::result::success,"paste constrained skeleton");
    require(p.core().constraints().size()==4,"paste duplicates both relations");
    std::vector<sm::object_id> duplicate_ids;
    for (const auto& [id,c] : p.core().constraints()) if (id!=triangle && id!=arbitrary) {
        duplicate_ids.push_back(id);
        if (auto r=c.rotation()) require(r->target_bone!=a && r->reference.bone_id!=b &&
            r->reference.kind==sm::rotation_reference_kind::bone,"paste remaps both arbitrary endpoints");
        if (auto t=c.triangle()) require(t->first_bone!=a && t->second_bone!=b,"paste remaps triangle endpoints");
    }
    p.undo(); require(p.core().constraints().size()==2,"paste undo removes copied relationships");
    p.redo(); for (auto id : duplicate_ids) require(p.core().constraint_by_id(id).has_value(),"paste redo preserves new relation IDs");
}
}
int main() {
    try { editor_history(); fan_history_and_copy(); std::cout << "constraint UI history passed\n"; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
