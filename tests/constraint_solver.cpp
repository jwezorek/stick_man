#include "core/sm_constraint_geometry.hpp"
#include "core/sm_fabrik.hpp"
#include "core/sm_project.hpp"
#include "core/sm_geometry_batch.hpp"
#include <numbers>
#include <iostream>
#include <stdexcept>
#include <cmath>
namespace {
constexpr double pi=std::numbers::pi;
void require(bool x,const char* s){if(!x)throw std::runtime_error(s);}
void near(sm::point a,sm::point b,const char* s,double eps=0.02){require(sm::distance(a,b)<eps,s);}
void angle(double a,double b,const char* s){require(std::abs(sm::angular_distance(a,b))<1e-7,s);}
struct fixture {
 sm::project p;
 sm::node_ref node(sm::point pt){return p.create_skeleton(pt).root_node();}
 sm::bone_ref link(sm::node_ref a,sm::node_ref b){return p.create_bone("bone",a,b).value();}
 void fan(sm::bone_ref a,sm::bone_ref b){require(p.add_rigid_triangle_constraint(a->id(),b->id()).has_value(),"fan add failed");}
 void limit(sm::bone_ref b,sm::rotation_reference r,sm::angle_range v){require(p.add_rotation_constraint(b->id(),r,v).has_value(),"rotation add failed");}
 void hard(){require(sm::constraint_geometry(p.topology()).validate(1e-7,false)==sm::result::success,"hard fan geometry violated");}
};
void angular_relations(){
 fixture f;auto p=f.node({0,0}),a=f.node({10,0}),b=f.node({10,10});auto x=f.link(p,a),y=f.link(a,b);
 f.limit(y,sm::rotation_reference::parent(),{pi/2,0});
 sm::constraint_geometry g(f.p.topology());
 require(g.allowed_angles(*x).contains(0),"inverse parent relation excludes actual angle");
 require(!g.allowed_angles(*x).contains(0.1),"inverse parent relation not applied");
 require(g.allowed_angles(*y).contains(pi/2),"parent relation wrong");
 auto q=f.node({30,0}),r=f.node({40,0});auto external=f.link(q,r);
 f.limit(x,sm::rotation_reference::bone(external->id()),{0,0});
 sm::constraint_geometry g2(f.p.topology());
 require(g2.allowed_angles(*external).contains(0)&&!g2.allowed_angles(*external).contains(pi/2),"arbitrary reference inverse wrong");
 f.limit(x,sm::rotation_reference::world(),{-0.5,1});
 f.limit(x,sm::rotation_reference::world(),{1,0.2});
 auto before=a->world_pos();
 require(sm::perform_fabrik(a,{5,8},p)==sm::result::unsatisfiable_constraints,"empty intersection must fail explicitly");
 near(a->world_pos(),before,"failed solve changed geometry",1e-9);
}
void either_arm_and_pins(){
 for(int driver=0;driver<2;++driver){
  fixture f;auto p=f.node({0,0}),a=f.node({10,0}),b=f.node({0,10}),unused=f.node({-10,0});
  auto x=f.link(p,a),y=f.link(p,b);f.link(p,unused);f.fan(x,y);
  auto target=driver?sm::point{-10,0}:sm::point{0,10};
  auto result=sm::perform_fabrik(driver?b:a,target,p);
  require(result==sm::result::fabrik_target_reached,"pinned fan target not reached");
  near(a->world_pos(),{0,10},"first fan arm wrong");near(b->world_pos(),{-10,0},"second fan arm wrong");
  near(unused->world_pos(),{-10,0},"unrelated branch across pin moved",1e-9);f.hard();
 }
}
void each_node_unpinned(){
 for(int driver=0;driver<3;++driver){
  fixture f;auto p=f.node({0,0}),a=f.node({10,0}),b=f.node({0,10});auto x=f.link(p,a),y=f.link(p,b);f.fan(x,y);
  auto node=driver==0?p:driver==1?a:b;auto target=node->world_pos()+sm::point{3,4};
  auto status=sm::perform_fabrik(node,target,{});require(status==sm::result::fabrik_target_reached,"free fan target not reached");
  near(node->world_pos(),target,"free fan drag target wrong");f.hard();
 }
}
void larger_fan_limits_cycles(){
 fixture f;auto p=f.node({0,0}),a=f.node({10,0}),b=f.node({0,10}),c=f.node({-10,0});auto x=f.link(p,a),y=f.link(p,b),z=f.link(p,c);
 f.fan(x,y);f.fan(y,z);f.fan(z,x);
 f.limit(x,sm::rotation_reference::world(),{0,pi/2});
 f.limit(y,sm::rotation_reference::world(),{pi/2,pi/4});
 sm::constraint_geometry g(f.p.topology());require(g.fan_members(*g.fan_for(x.ptr())).size()==3,"shared arms not merged");
 require(g.project_fan(*g.fan_for(x.ptr()),*x,*p,{0,10})==sm::result::success,"fan limits projection failed");
 angle(x->world_rotation(),pi/4,"member constraints not intersected");f.hard();
 auto records=f.p.constraints();auto entry=records.begin();while(!entry->second.triangle())++entry;
 auto tri=*entry->second.triangle();tri.relative_angle+=0.2;
 require(f.p.update_constraint(entry->first,tri)==sm::result::inconsistent_constraints,"inconsistent exact cycle accepted");
}
void pinned_tips(){
 fixture f;auto p=f.node({0,0}),a=f.node({10,0}),b=f.node({0,10});auto x=f.link(p,a),y=f.link(p,b);f.fan(x,y);
 auto status=sm::perform_fabrik({{p,{3,3}}},{a,b});
 require(status==sm::result::fabrik_converged||status==sm::result::fabrik_target_reached||status==sm::result::fabrik_mixed,"fixed triangle returned invalid failure");
 near(a->world_pos(),{10,0},"first pinned tip moved",1e-8);near(b->world_pos(),{0,10},"second pinned tip moved",1e-8);f.hard();
 sm::constraint_geometry g(f.p.topology());auto fan=*g.fan_for(x.ptr());
 require(g.project_fan(fan,*x,*p,{10,0},true,{{p.ptr(),{0,0}},{a.ptr(),{20,0}}})==sm::result::unsatisfiable_constraints,"impossible fixed radius accepted");
}
void articulated_fan(){
 fixture f;auto root=f.node({-10,0}),pivot=f.node({0,0}),a=f.node({10,0}),b=f.node({0,10}),tip=f.node({20,0}),other=f.node({0,20});
 f.link(root,pivot);auto x=f.link(pivot,a),y=f.link(pivot,b);f.link(a,tip);f.link(b,other);f.fan(x,y);
 auto status=sm::perform_fabrik(tip,{15,8},root);
 require(status==sm::result::fabrik_target_reached||status==sm::result::fabrik_converged||status==sm::result::fabrik_mixed,"articulated fan solve failed");
 near(root->world_pos(),{-10,0},"articulated root moved",1e-8);f.hard();
 for(auto bone:x->owner().bones())require(std::abs(bone->scaled_length()-10)<0.02,"articulated bone length changed");
}
void unrelated_ranges_do_not_block_region(){
 fixture f;auto p=f.node({0,0}),a=f.node({10,0}),q=f.node({30,0}),b=f.node({40,0});auto x=f.link(p,a),y=f.link(q,b);
 f.limit(y,sm::rotation_reference::world(),{pi/2,0});
 require(sm::perform_fabrik(a,{0,10},p)==sm::result::fabrik_target_reached,"unrelated range rejected active solve");
 near(b->world_pos(),{40,0},"external skeleton was changed",1e-9);
}
void multiple_effectors_and_remote_pins(){
 fixture f;auto p=f.node({0,0}),a=f.node({10,0}),b=f.node({0,10}),tip=f.node({0,20});auto x=f.link(p,a),y=f.link(p,b);f.link(b,tip);f.fan(x,y);
 auto status=sm::perform_fabrik({{a,{0,10}},{b,{-10,0}}},{p});
 require(status==sm::result::fabrik_target_reached,"merged fan effectors missed");f.hard();
 near(a->world_pos(),{0,10},"merged first target wrong");near(b->world_pos(),{-10,0},"merged second target wrong");
 const auto saved=tip->world_pos();
 status=sm::perform_fabrik({{a,{3,9}}},{p,tip});
 near(tip->world_pos(),saved,"remote pin moved",1e-8);near(p->world_pos(),{0,0},"pivot pin moved",1e-8);f.hard();
}
void inverse_reference_solve(){
 fixture f;auto p=f.node({0,0}),a=f.node({10,0}),q=f.node({30,0}),b=f.node({40,0});auto x=f.link(p,a),y=f.link(q,b);
 f.limit(x,sm::rotation_reference::bone(y->id()),{0,0});
 auto status=sm::perform_fabrik(b,{30,10},q);
 require(status==sm::result::fabrik_mixed||status==sm::result::fabrik_converged,"inverse solve did not settle");
 angle(y->world_rotation(),0,"reference endpoint inverse relation ignored");near(a->world_pos(),{10,0},"external target endpoint moved",1e-9);
}
void nonfan_parent_rotation(){
 fixture f;auto root=f.node({-10,0}),p=f.node({0,0}),a=f.node({10,0}),b=f.node({0,10});auto parent=f.link(root,p),x=f.link(p,a),y=f.link(p,b);f.fan(x,y);
 parent->rotate_by(pi/2);near(p->world_pos(),{-10,10},"parent did not rotate");f.hard();
 angle(x->world_rotation(),pi/2,"fan did not follow articulated parent");
}

void nested_fans_remain_exact(){
 for(int driver=0;driver<5;++driver){
  fixture f;auto root=f.node({-10,0}),p=f.node({0,0}),a=f.node({10,0}),b=f.node({0,10}),c=f.node({20,0}),d=f.node({10,10});
  f.link(root,p);auto x=f.link(p,a),y=f.link(p,b),z=f.link(a,c),w=f.link(a,d);f.fan(x,y);f.fan(z,w);
  sm::node_ref moving=driver==0?p:driver==1?a:driver==2?b:driver==3?c:d;
  auto status=sm::perform_fabrik(moving,moving->world_pos()+sm::point{2,3},root);
  require(status==sm::result::fabrik_target_reached||status==sm::result::fabrik_converged||status==sm::result::fabrik_mixed,"nested fan solve failed");
  f.hard();near(root->world_pos(),{-10,0},"nested root pin moved",1e-9);
 }
}
void fan_velocity_intersection_and_infeasible_pins(){
 fixture f;auto p=f.node({0,0}),a=f.node({10,0}),b=f.node({0,10});auto x=f.link(p,a),y=f.link(p,b);f.fan(x,y);
 sm::constraint_geometry g(f.p.topology());auto fan=*g.fan_for(x.ptr());
 auto status=g.project_fan(fan,*x,*p,{0,10},true,{},0.1,{{x.ptr(),0},{y.ptr(),pi/2}});
 require(status==sm::result::success,"fan velocity clamp failed");angle(x->world_rotation(),0.1,"fan velocity intersection wrong");f.hard();
 auto saved_a=a->world_pos(),saved_b=b->world_pos();
 status=g.project_fan(fan,*x,*p,{0,10},true,{{p.ptr(),{0,0}},{a.ptr(),{10,0}},{b.ptr(),{0,-10}}});
 require(status==sm::result::unsatisfiable_constraints,"opposite handedness pinned pose accepted");
 near(a->world_pos(),saved_a,"failed fan projection changed first tip",1e-9);near(b->world_pos(),saved_b,"failed fan projection changed second tip",1e-9);
}

void rotating_subtree_relations(){
 fixture f;auto p=f.node({0,0}),a=f.node({10,0}),b=f.node({0,10}),tip=f.node({20,0});auto x=f.link(p,a),y=f.link(p,b),child=f.link(a,tip);f.fan(x,y);
 f.limit(x,sm::rotation_reference::bone(child->id()),{0,0});
 x->rotate_by(pi/2);angle(x->world_rotation(),pi/2,"subtree internal relation clamped against stale orientation");f.hard();
 auto q=f.node({40,0}),r=f.node({50,0});auto unrelated=f.link(q,r);f.limit(unrelated,sm::rotation_reference::world(),{pi/2,0});
 x->rotate_by(pi/2);angle(x->world_rotation(),pi,"unrelated range rejected direct fan rotation");f.hard();
}

void direct_edits(){
 fixture f;auto p=f.node({0,0}),a=f.node({10,0}),b=f.node({0,10}),tip=f.node({0,20});auto x=f.link(p,a),y=f.link(p,b);f.link(b,tip);f.fan(x,y);
 x->rotate_by(pi/2,{},true);near(a->world_pos(),{0,10},"direct arm rotate failed");near(b->world_pos(),{-10,0},"direct sibling rotate failed");near(tip->world_pos(),{-20,0},"sibling descendant not carried");f.hard();
 x->set_length(15);require(std::abs(x->scaled_length()-15)<1e-8,"length edit ignored");f.hard();
}
}
int main(){try{
 angular_relations();either_arm_and_pins();each_node_unpinned();larger_fan_limits_cycles();pinned_tips();articulated_fan();direct_edits();unrelated_ranges_do_not_block_region();multiple_effectors_and_remote_pins();inverse_reference_solve();nonfan_parent_rotation();nested_fans_remain_exact();fan_velocity_intersection_and_infeasible_pins();rotating_subtree_relations();
 std::cout<<"PASS constraint_solver\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}

