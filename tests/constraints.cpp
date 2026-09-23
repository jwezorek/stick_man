#include "core/sm_project.hpp"
#include "core/sm_constraint.hpp"
#include "json.hpp"
#include <cmath>
#include <iostream>
#include <numbers>
#include <stdexcept>
namespace {
constexpr double pi = std::numbers::pi;
void require(bool b, const char* m) { if (!b) throw std::runtime_error(m); }
struct fixture {
    sm::project p;
    sm::bone_ref a, b, c, child, external;
    sm::bone_ref link(sm::node& root, sm::point tip, const char* name) {
        auto& end = p.create_skeleton(tip).root_node();
        return p.create_bone(name, root, end).value();
    }
    fixture() : a(link(p.create_skeleton({0,0}).root_node(), {10,0}, "a")),
        b(link(a->parent_node(), {0,10}, "b")), c(link(a->parent_node(), {-10,0}, "c")),
        child(link(a->child_node(), {20,0}, "child")),
        external(link(p.create_skeleton({100,0}).root_node(), {110,0}, "external")) {}
};
sm::object_id rotation(sm::project& p, sm::object_id target, sm::rotation_reference reference, const char* name = "rotation") {
    auto r = p.add_rotation_constraint(target, reference, {-0.5,1}, name);
    require(r.has_value(), "valid rotation rejected"); return r->get().id();
}
void persistence() {
    fixture f;
    auto world = rotation(f.p,f.a->id(),sm::rotation_reference::world(),"world");
    auto parent = rotation(f.p,f.child->id(),sm::rotation_reference::parent(),"parent");
    auto arbitrary = rotation(f.p,f.a->id(),sm::rotation_reference::bone(f.external->id()),"arbitrary");
    auto triangle = f.p.add_rigid_triangle_constraint(f.b->id(),f.a->id(),"clockwise");
    require(triangle.has_value(),"triangle rejected");
    require(std::abs(triangle->get().triangle()->relative_angle + pi/2) < 1e-12,"signed rest angle lost");
    require(f.p.constraints_for_bone(f.a->id()).size() == 3,"multiple constraints lookup failed");
    require(f.p.constraints_for_bone(f.external->id()).size() == 1,"reference lookup failed");
    require(!f.p.constraint_by_id(sm::object_id::generate()),"missing lookup succeeded");
    require(std::holds_alternative<sm::const_constraint_ref>(std::as_const(f.p).get(arbitrary)),"generic lookup wrong type");
    f.p.rename(world,"renamed world");
    auto json = sm::constraints_to_json(f.p.constraints());
    bool found = false;
    for (const auto& j : json) if (j.at("id") == arbitrary.to_string()) {
        found = true;
        require(j.at("reference").at("type") == "bone","JSON reference kind lost");
        require(j.at("reference").at("bone") == f.external->id().to_string(),"JSON reference ID lost");
    }
    require(found,"JSON omitted reference");
    auto bytes = f.p.serialize(); require(bytes.has_value(),"serialize failed");
    sm::project q; require(q.deserialize(*bytes) == sm::project_result::success,"roundtrip failed");
    require(sm::constraints_to_json(q.constraints()) == json,"roundtrip changed IDs, names, ranges, references or signed rest");
    require(q.constraint_by_id(world)->get().name() == "renamed world","world name lost");
    require(q.constraint_by_id(parent)->get().rotation()->reference.kind == sm::rotation_reference_kind::parent,"parent reference lost");
}
void validation() {
    fixture f; auto missing = sm::object_id::generate();
    require(!f.p.add_rotation_constraint(missing,sm::rotation_reference::world(),{0,1}),"missing target accepted");
    require(!f.p.add_rotation_constraint(f.a->id(),sm::rotation_reference::bone(missing),{0,1}),"missing reference accepted");
    require(!f.p.add_rotation_constraint(f.a->id(),sm::rotation_reference::bone(f.a->id()),{0,1}),"self reference accepted");
    require(!f.p.add_rotation_constraint(f.a->id(),sm::rotation_reference::parent(),{0,1}),"parentless reference accepted");
    require(!f.p.add_rigid_triangle_constraint(f.a->id(),missing),"missing triangle bone accepted");
    require(!f.p.add_rigid_triangle_constraint(f.a->id(),f.a->id()),"self triangle accepted");
    require(!f.p.add_rigid_triangle_constraint(f.a->id(),f.child->id()),"non-sibling triangle accepted");
    auto add = [&](sm::object_id a, sm::object_id b, double rest) {
        return f.p.add_constraint(sm::constraint(sm::object_id::generate(),"edge",sm::rigid_triangle_constraint{a,b,rest}));
    };
    require(add(f.a->id(),f.b->id(),pi/2).has_value(),"first edge rejected");
    require(!add(f.b->id(),f.a->id(),-pi/2),"reversed duplicate accepted");
    require(add(f.b->id(),f.c->id(),pi/2).has_value(),"second edge rejected");
    auto bad = add(f.c->id(),f.a->id(),0);
    require(!bad && bad.error() == sm::result::inconsistent_constraints,"inconsistent cycle accepted");
    require(f.p.constraints().size() == 2,"failed insertion mutated constraints");
    require(add(f.c->id(),f.a->id(),pi).has_value(),"consistent modulo-full-turn cycle rejected");
}
void collisions() {
    fixture f; auto cid = rotation(f.p,f.a->id(),sm::rotation_reference::world());
    std::vector<sm::const_skel_ref> skeletons{f.a->owner()};
    auto character = f.p.create_character(skeletons); require(character.has_value(),"character fixture failed");
    auto def = sm::rotation_constraint{f.a->id(),sm::rotation_reference::world(),{0,1}};
    for (auto id : {f.a->id(),f.a->parent_node().id(),f.a->owner().id(),character->get().id(),cid}) {
        auto result = f.p.add_constraint(sm::constraint(id,"collision",def));
        require(!result && result.error() == sm::result::duplicate_id,"global ID collision accepted");
    }
    require(f.p.constraints().size() == 1 && f.p.has_unique_object_ids(),"collision damaged registry");
}
void deletion() {
    fixture f; auto keep = rotation(f.p,f.a->id(),sm::rotation_reference::world());
    auto remove = rotation(f.p,f.a->id(),sm::rotation_reference::bone(f.external->id()));
    require(f.p.delete_skeleton(f.external->owner().id()) == sm::result::success,"reference deletion failed");
    require(!f.p.constraint_by_id(remove) && f.p.constraint_by_id(keep).has_value(),"reference cleanup not selective");
    require(f.p.delete_skeleton(f.a->owner().id()) == sm::result::success,"target deletion failed");
    require(f.p.constraints().empty(),"dangling constraints after deletion");
}
void copies() {
    fixture f;
    auto internal = rotation(f.p,f.a->id(),sm::rotation_reference::bone(f.b->id()),"internal");
    auto cross = rotation(f.p,f.a->id(),sm::rotation_reference::bone(f.external->id()),"cross");
    sm::topology scratch_isolated;
    require(f.a->owner().copy_to(scratch_isolated).has_value(),"scratch isolated copy failed");
    require(scratch_isolated.constraints().contains(internal) && !scratch_isolated.constraints().contains(cross),
        "scratch copy retained dangling external reference");
    sm::project isolated;
    require(isolated.copy_skeleton(f.a->owner()).has_value(),"isolated copy failed");
    require(isolated.constraint_by_id(internal).has_value() && !isolated.constraint_by_id(cross),"isolated copy did not omit dangling external ref");
    sm::project copied;
    require(copied.copy_skeleton(f.external->owner()).has_value(),"reference copy failed");
    require(copied.copy_skeleton(f.a->owner()).has_value(),"target copy failed");
    require(copied.constraint_by_id(internal).has_value() && copied.constraint_by_id(cross).has_value(),"copy lost resolvable refs");
    std::unordered_map<sm::object_id,sm::object_id> ids;
    for (auto skel : {sm::const_skel_ref(f.a->owner()),sm::const_skel_ref(f.external->owner())}) {
        ids.emplace(skel->id(),sm::object_id::generate());
        for(auto n:skel->nodes()) ids.emplace(n->id(),sm::object_id::generate());
        for(auto b:skel->bones()) ids.emplace(b->id(),sm::object_id::generate());
    }
    ids.emplace(internal,sm::object_id::generate()); ids.emplace(cross,sm::object_id::generate());
    sm::project remapped;
    require(remapped.copy_skeleton(f.external->owner(),ids).has_value(),"remap reference copy failed");
    require(remapped.copy_skeleton(f.a->owner(),ids).has_value(),"remap target copy failed");
    auto ci = remapped.constraint_by_id(ids.at(internal)), cc = remapped.constraint_by_id(ids.at(cross));
    require(ci.has_value() && cc.has_value(),"remap lost IDs");
    require(ci->get().rotation()->target_bone == ids.at(f.a->id()) && ci->get().rotation()->reference.bone_id == ids.at(f.b->id()),"internal reference remap failed");
    require(cc->get().rotation()->reference.bone_id == ids.at(f.external->id()),"cross reference remap failed");
    sm::topology scratch;
    require(f.external->owner().copy_to(scratch).has_value(),"duplicate reference fixture failed");
    auto clone = f.a->owner().duplicate_to(scratch); require(clone.has_value(),"duplicate failed");
    require(scratch.constraints().size() == 2,"duplicate lost constraints");
    for(auto& [id,c]:scratch.constraints()) {
        auto r = c.rotation(); require(id != internal && id != cross,"duplicate reused constraint ID");
        require(r && r->target_bone != f.a->id() && clone->get().get<sm::bone>(r->target_bone).has_value(),"duplicate target remap failed");
        if(c.name() == "internal") require(r->reference.bone_id != f.b->id() && clone->get().get<sm::bone>(r->reference.bone_id).has_value(),"duplicate internal reference remap failed");
        else require(r->reference.bone_id == f.external->id(),"duplicate external reference changed");
    }
}
}
int main() {
    int failures = 0;
    using test_case = std::pair<const char*,void(*)()>;
    for(auto [name,test] : {test_case{"persistence",persistence},{"validation",validation},{"collisions",collisions},{"deletion",deletion},{"copies",copies}}) {
        try { test(); std::cout << name << " passed\n"; }
        catch(const std::exception& e) { std::cerr << name << ": " << e.what() << '\n'; ++failures; }
    }
    return failures ? 1 : 0;
}
