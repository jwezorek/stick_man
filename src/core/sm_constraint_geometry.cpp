#include "sm_constraint_geometry.hpp"
#include "sm_geometry_batch.hpp"
#include "sm_skeleton.hpp"
#include "sm_visit.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>
#include <unordered_set>
namespace sm {
namespace {
point radial(point p,double length,double angle) {return {p.x+length*std::cos(angle),p.y+length*std::sin(angle)};}
}
constraint_geometry::constraint_geometry(const topology& t):constraint_geometry(t,t.constraints()) {}
constraint_geometry::constraint_geometry(const topology& t,const constraint_map& records) {
 status_=validate_constraints(t,records); if(status_!=result::success)return;
 std::map<object_id,std::vector<std::pair<bone*,double>>> graph;
 for(const auto& [id,c]:records) {
  if(auto rot=c.rotation()) {
   auto target=t.get<bone>(rot->target_bone); bone* reference=nullptr;
   if(rot->reference.kind==rotation_reference_kind::parent) reference=target->get().parent_bone()->ptr();
   if(rot->reference.kind==rotation_reference_kind::bone) reference=t.get<bone>(rot->reference.bone_id)->ptr();
   relations_.push_back({target->ptr(),reference,angle_set(rot->allowed)});
  } else if(auto tri=c.triangle()) {
   auto a=t.get<bone>(tri->first_bone)->ptr(),b=t.get<bone>(tri->second_bone)->ptr();
   graph[a->id()].push_back({b,tri->relative_angle});graph[b->id()].push_back({a,-tri->relative_angle});
  }
 }
 for(const auto& [id,edges]:graph) {
  auto root=t.get<bone>(id)->ptr(); if(membership_.contains(root))continue;
  size_t index=fans_.size(); fans_.push_back({&root->parent_node(),{}});
  std::vector<std::pair<bone*,double>> pending{{root,0}};
  while(!pending.empty()) {
   auto [b,offset]=pending.back();pending.pop_back();
   if(auto it=membership_.find(b);it!=membership_.end()) {
    if(std::abs(angular_distance(offset,it->second.second))>1e-8) status_=result::inconsistent_constraints;
    continue;
   }
   membership_[b]={index,offset};fans_.back().members.push_back({b,offset,b->scaled_length()});
   for(auto [other,delta]:graph.at(b->id()))pending.push_back({other,normalize_angle(offset+delta)});
  }
 }
}
std::optional<size_t> constraint_geometry::fan_for(const bone* b) const {
 auto it=membership_.find(b);if(it==membership_.end())return {};return it->second.first;
}
std::vector<bone*> constraint_geometry::fan_members(size_t i) const {
 std::vector<bone*> out;for(auto m:fans_.at(i).members)out.push_back(m.value);return out;
}
angle_set constraint_geometry::allowed_angles(bone& b,bool enabled,const std::unordered_set<bone*>* rotating) const {
 if(status_!=result::success)return angle_set::empty();
 angle_set allowed;if(!enabled)return allowed;
 auto selected=fan_for(&b);double selected_offset=selected&&!rotating?membership_.at(&b).second:0;
 auto included=[&](bone* v){return v&&(v==&b||(rotating?rotating->contains(v):(selected&&fan_for(v)==selected)));};
 auto offset=[&](bone* v){return rotating?angular_distance(b.world_rotation(),v->world_rotation()):(selected?membership_.at(v).second:0);};
 for(const auto& rel:relations_) {
  bool target=included(rel.target),reference=included(rel.reference);
  if(target&&reference) {
   if(!rel.allowed.contains(offset(rel.target)-offset(rel.reference)))return angle_set::empty();
  } else if(target) {
   double anchor=rel.reference?rel.reference->world_rotation():0;
   allowed=allowed.intersect(rel.allowed.shifted(anchor-offset(rel.target)+selected_offset));
  } else if(reference) {
   allowed=allowed.intersect(rel.allowed.negated().shifted(rel.target->world_rotation()-offset(rel.reference)+selected_offset));
  }
 }
 return allowed;
}
result constraint_geometry::project_fan(size_t index,bone& driver,node& leader,point proposed,bool limits,
 const std::unordered_map<node*,point>& pins,double max_delta,const std::unordered_map<bone*,double>& originals) {
 if(status_!=result::success)return status_;
 auto& f=fans_.at(index);double off=membership_.at(&driver).second;
 geometry_batch batch(driver.owner().owner());
 auto allowed=allowed_angles(driver,limits).shifted(-off);
 if(max_delta>0)for(auto m:f.members)if(auto it=originals.find(m.value);it!=originals.end())
  allowed=allowed.intersect(angle_set({it->second-max_delta-m.offset,2*max_delta}));
 double theta=(&leader==f.pivot?angle_from_u_to_v(leader.world_pos(),proposed):angle_from_u_to_v(proposed,leader.world_pos()))-off;
 point pivot=f.pivot->world_pos();bool fixed_pivot=pins.contains(f.pivot);
 if(fixed_pivot)pivot=pins.at(f.pivot);
 std::vector<std::pair<member,point>> fixed;
 for(auto m:f.members)if(auto it=pins.find(&m.value->child_node());it!=pins.end())fixed.push_back({m,it->second});
 if(fixed_pivot) {
  for(auto [m,pt]:fixed) {
   if(std::abs(distance(pivot,pt)-m.length)>1e-7)return result::unsatisfiable_constraints;
   allowed=allowed.intersect(angle_set({angle_from_u_to_v(pivot,pt)-m.offset,0}));
  }
 } else if(fixed.size()>1) {
  auto [m0,p0]=fixed.front();
  auto v0=radial({0,0},m0.length,m0.offset);
  for(size_t i=1;i<fixed.size();++i) {
   auto [m1,p1]=fixed[i];auto v1=radial({0,0},m1.length,m1.offset);
   if(std::abs(distance(p0,p1)-distance(v0,v1))>1e-7)return result::unsatisfiable_constraints;
   if(distance(v0,v1)>1e-10)allowed=allowed.intersect(angle_set({angle_from_u_to_v(p0,p1)-angle_from_u_to_v(v0,v1),0}));
  }
 }
 auto clamped=allowed.closest_angle(theta);if(!clamped)return result::unsatisfiable_constraints;theta=*clamped;
 if(!fixed_pivot) {
  if(!fixed.empty()) {auto [m,pt]=fixed.front();pivot=radial(pt,-m.length,theta+m.offset);}
  else if(&leader!=f.pivot) {
   auto it=std::find_if(f.members.begin(),f.members.end(),[&](auto m){return m.value==&driver;});
   pivot=radial(leader.world_pos(),-it->length,theta+off);
  }
 }
 for(auto [m,pt]:fixed)if(distance(radial(pivot,m.length,theta+m.offset),pt)>1e-7)return result::unsatisfiable_constraints;
 f.pivot->set_world_pos(pivot);for(auto m:f.members)m.value->child_node().set_world_pos(radial(pivot,m.length,theta+m.offset));
 return batch.commit();
}
result constraint_geometry::validate(double tolerance,bool limits,const std::unordered_set<bone*>* active) const {
 if(status_!=result::success)return status_;
 for(const auto& f:fans_) {
  if(active&&std::none_of(f.members.begin(),f.members.end(),[&](auto m){return active->contains(m.value);}))continue;
  auto first=f.members.front();double theta=first.value->world_rotation()-first.offset;
  for(auto m:f.members)if(std::abs(angular_distance(m.value->world_rotation(),theta+m.offset))>tolerance||
   std::abs(m.value->scaled_length()-m.length)>tolerance)return result::unsatisfiable_constraints;
 }
 if(limits)for(const auto& r:relations_) {
  if(active&&!active->contains(r.target)&&(!r.reference||!active->contains(r.reference)))continue;
  const double angle=r.target->world_rotation()-(r.reference?r.reference->world_rotation():0);
  if(r.allowed.contains(angle))continue;
  // Use the same numerical angular tolerance as fan validation and the IK
  // candidate check. Exact interval membership rejects roundoff at a limit.
  const auto closest=r.allowed.closest_angle(angle);
  if(!closest||std::abs(angular_distance(angle,*closest))>tolerance)return result::unsatisfiable_constraints;
 }
 return result::success;
}
result rotate_constrained_bone(bone& b,double theta,bool descendants,maybe_node_ref axis) {
 geometry_batch batch(b.owner().owner());
 constraint_geometry g(b.owner().owner());if(g.status()!=result::success)return g.status();
 std::vector<bone*> members{&b};if(auto fan=g.fan_for(&b))members=g.fan_members(*fan);
 std::unordered_map<node*,point> saved;
 for(auto member:members) {
  if(descendants||members.size()>1)visit_nodes(member->child_node(),[&](node& n){saved.try_emplace(&n,n.world_pos());return visit_result::continue_traversal;});
  else saved.try_emplace(&member->child_node(),member->child_node().world_pos());
 }
 if(axis&&axis->ptr()!=&b.parent_node()) {
  visit_nodes_and_bones(b.parent_node(),[&](node& n){saved.try_emplace(&n,n.world_pos());return visit_result::continue_traversal;});
 }
 std::unordered_set<bone*> rotating(members.begin(),members.end()),affected;
 for(auto [n,pt]:saved)for(auto edge:n->adjacent_bones()) {
  affected.insert(edge.ptr());
  if(saved.contains(&edge->parent_node())&&saved.contains(&edge->child_node()))rotating.insert(edge.ptr());
 }
 auto closest=g.allowed_angles(b,true,&rotating).closest_angle(b.world_rotation()+theta);
 if(!closest)return result::unsatisfiable_constraints;
 double delta=angular_distance(b.world_rotation(),*closest);
 auto mat=rotate_about_point_matrix(axis?axis->get().world_pos():b.parent_node().world_pos(),delta);
 for(auto [n,pt]:saved)n->set_world_pos(transform(pt,mat));
 auto result=g.validate(1e-8,true,&affected);if(result!=result::success)for(auto [n,pt]:saved)n->set_world_pos(pt);
 return result == sm::result::success ? batch.commit() : result;
}
}
