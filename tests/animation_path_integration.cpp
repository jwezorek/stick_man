#include "model/project.hpp"
#include "core/sm_animation.hpp"
#include "json.hpp"
#include <iostream>
#include <stdexcept>
#include <cmath>

namespace {
void require(bool ok,const char* msg) { if (!ok) throw std::runtime_error(msg); }
void near(sm::point a, sm::point b,double eps,const char* msg) {
    require(sm::distance(a,b)<eps,msg);
}

void authoring_persistence_and_scrubbing() {
    mdl::project model;
    auto& root=model.core().create_skeleton({0,0}).root_node();
    auto& tip=model.core().create_skeleton({10,0}).root_node();
    const auto root_id=root.id(),tip_id=tip.id();
    auto bone=model.core().create_bone("arm",root,tip);
    require(bone.has_value(),"fixture bone");
    const auto bone_id=bone->get().id();
    auto character=model.core().create_character(std::vector<sm::const_skel_ref>{bone->get().owner()});
    require(character.has_value(),"fixture character");
    const auto character_id=character->get().id();
    sm::animation animation;
    animation.name="Reach";
    const auto aid=animation.id;
    model.core().animation_data(character_id).animations.push_back(animation);
    require(model.begin_animation_session(character_id,aid)==sm::result::success,"begin session");
    require(model.add_animation_keyframe()==sm::result::success,"first key");
    const auto first=*model.animation_session_keyframe();
    require(!model.animation_has_outgoing_transition(),"terminal key must not have transition");
    require(model.set_animation_path(tip_id,sm::animation_path{})==sm::result::invalid_animation,
        "last frame accepted path without transition");
    require(model.add_animation_keyframe()==sm::result::success,"second key");
    // Rotate the one bone to its 90-degree pose; the two endpoints retain the bone length.
    model.transform_node_positions({{tip_id,{10,0}}},{{tip_id,{0,10}}});
    auto* a=model.core().animation_data(character_id).find_animation(aid);
    auto to=sm::animation_pose_node(a->keyframes[1].pose,model.core().topology(),
        character->get().rig().skeleton_ids(),tip_id);
    require(to.has_value(),"destination pose missing tip");
    require(model.select_animation_keyframe(first)==sm::result::success,"select source key");
    require(model.animation_has_outgoing_transition(),"source key should own transition");
    auto geometry=model.animation_session_path_context(tip_id);
    require(geometry.has_value(),"path geometry unavailable");
    near(geometry->frame.to_world(geometry->end),*to,1e-6,"destination ghost differs from pose");

    sm::animation_path path;
    path.node=tip_id;
    path.reset(geometry->start,geometry->end);
    path.knots[0].handle_out={0,8};
    path.knots[1].handle_in={-4,0};
    require(model.set_animation_path(tip_id,path)==sm::result::success,"create path");
    require(a->transitions[0].paths.size()==1,"path is not transition-local");
    model.undo();
    require(a->transitions[0].paths.empty(),"creation undo failed");
    require(model.redo()==sm::result::success,"creation redo failed");
    require(a->transitions[0].paths.contains(tip_id),"creation redo lost path");

    // All paths are cubic Bezier splines. Adding a knot edits a one-segment
    // curve into a two-segment spline without changing its geometry.
    auto changed=path;
    constexpr double split=0.35;
    const auto insertion_point=path.at_parameter(split,geometry->start,geometry->end);
    changed.insert_knot(0,split,geometry->start,geometry->end);
    require(changed.knots.size()==3 && changed.is_smooth(),"knot insertion failed");
    near(changed.knots[1].position,insertion_point,1e-8,"insertion moved the curve");
    require(model.set_animation_path(tip_id,changed)==sm::result::success,"edit spline knots");
    require(a->transitions[0].paths.at(tip_id).knots.size()==3,"knot insertion edit failed");
    require(a->transitions[0].paths.at(tip_id).is_smooth(),"inserted knot is not G1-smooth");
    model.undo();
    require(a->transitions[0].paths.at(tip_id).knots.size()==2,"knot insertion undo failed");
    near(a->transitions[0].paths.at(tip_id).knots[0].handle_out,path.knots[0].handle_out,
        1e-8,"undo did not restore original path geometry");
    require(model.redo()==sm::result::success,"knot insertion redo failed");
    require(a->transitions[0].paths.at(tip_id).knots.size()==3,"redo lost inserted knot");

    const auto& rig=character->get().rig().skeleton_ids();
    auto evaluated=sm::sample_constrained_pose(*a,0.2,model.core().topology(),rig,bone_id);
    require(evaluated && *evaluated,"path-constrained scrubbing failed");
    auto late=sm::sample_constrained_pose(*a,0.35,model.core().topology(),rig,bone_id);
    require(late && *late,"direct late seek failed");
    auto again=sm::sample_constrained_pose(*a,0.2,model.core().topology(),rig,bone_id);
    require(again && *again,"repeat seek failed");
    near((**again).pose.root_positions.at(root_id),
        (**evaluated).pose.root_positions.at(root_id),1e-7,"scrubbing depended on earlier samples");

    const auto json=sm::animation_assets_to_json(model.core().animation_data(character_id));
    const auto loaded=sm::animation_assets_from_json(json);
    const auto* roundtrip=loaded.find_animation(aid);
    require(roundtrip && roundtrip->transitions[0].paths.size()==1,"path serialization failed");
    require(roundtrip->transitions[0].paths.at(tip_id).knots.size()==3,"spline geometry not persisted");

    model.end_animation_session();
    const auto bytes=model.core().serialize();
    require(bytes.has_value(),"project package serialize failed");
    sm::project loaded_project;
    require(loaded_project.deserialize(*bytes)==sm::project_result::success,"project package path roundtrip failed");
    auto& saved=loaded_project.animation_data(character_id);
    require(saved.find_animation(aid)->transitions[0].paths.contains(tip_id),"package dropped path");
    auto broken=loaded;
    auto* broken_a=broken.find_animation(aid);
    auto entry=broken_a->transitions[0].paths.extract(tip_id);
    auto bogus=sm::object_id::generate();
    entry.key()=bogus;
    entry.mapped().node=bogus;
    broken_a->transitions[0].paths.insert(std::move(entry));
    bool rejected=false;
    try {broken.validate(model.core().topology(),rig);} catch(const std::invalid_argument&) {rejected=true;}
    require(rejected,"invalid node reference accepted");
}
}
int main() {
    try {authoring_persistence_and_scrubbing();std::cout<<"animation path integration passed\n";}
    catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
