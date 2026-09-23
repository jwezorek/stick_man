#pragma once
#include "sm_constraint.hpp"
#include "sm_angle_set.hpp"
#include <unordered_map>
#include <unordered_set>
namespace sm {
class constraint_geometry {
 struct member { bone* value; double offset, length; };
 struct fan { node* pivot; std::vector<member> members; };
 struct relation { bone* target; bone* reference; angle_set allowed; };
 std::vector<fan> fans_;
 std::unordered_map<const bone*,std::pair<size_t,double>> membership_;
 std::vector<relation> relations_;
 result status_=result::success;
public:
 explicit constraint_geometry(const topology&);
 constraint_geometry(const topology&,const constraint_map&);
 result status() const {return status_;}
 std::optional<size_t> fan_for(const bone*) const;
 std::vector<bone*> fan_members(size_t) const;
 angle_set allowed_angles(bone&,bool rotation_limits=true,const std::unordered_set<bone*>* rotating=nullptr) const;
 result project_fan(size_t,bone& driver,node& leader,point proposed_follower,bool rotation_limits=true,
  const std::unordered_map<node*,point>& pins={},double max_ang_delta=0,
  const std::unordered_map<bone*,double>& original_rotations={});
 result validate(double tolerance=1e-8,bool rotation_limits=true,const std::unordered_set<bone*>* active=nullptr) const;
};
result rotate_constrained_bone(bone&,double theta,bool descendants=true,maybe_node_ref axis={});
}
