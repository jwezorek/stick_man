#include "sm_project.hpp"
#include <cmath>

sm::project::project() { topology_.project_ = this; }
sm::expected_constraint sm::project::constraint_by_id(object_id id) const {
    auto it = constraints().find(id);
    if (it == constraints().end()) return std::unexpected(result::not_found);
    return const_constraint_ref(it->second);
}
std::vector<sm::const_constraint_ref> sm::project::constraints_for_bone(object_id id) const {
    std::vector<const_constraint_ref> out;
    for (auto& [cid,c] : constraints()) if (c.references(id)) out.emplace_back(c);
    return out;
}
sm::expected_constraint sm::project::add_constraint(const constraint& value) {
    if (!ensure_object_index() || objects_.contains(value.id())) return std::unexpected(result::duplicate_id);
    auto proposed = constraints(); proposed.emplace(value.id(),value);
    auto status = validate_constraints(topology_,proposed);
    if (status != result::success) return std::unexpected(status);
    topology_.constraints_.emplace(value.id(),value);
    invalidate_object_index();
    return constraint_by_id(value.id());
}
sm::expected_constraint sm::project::add_rotation_constraint(object_id target, rotation_reference reference,
        angle_range allowed, std::string name) {
    if (!ensure_object_index()) return std::unexpected(result::duplicate_id);
    object_id id;
    do { id = object_id::generate(); } while (objects_.contains(id));
    return add_constraint({id,std::move(name),rotation_constraint{target,reference,allowed}});
}
sm::expected_constraint sm::project::add_rigid_triangle_constraint(object_id first, object_id second, std::string name) {
    auto a = topology_.get<bone>(first), b = topology_.get<bone>(second);
    if (!a || !b) return std::unexpected(result::not_found);
    if (!ensure_object_index()) return std::unexpected(result::duplicate_id);
    object_id id;
    do { id = object_id::generate(); } while (objects_.contains(id));
    return add_constraint({id,std::move(name),rigid_triangle_constraint{
        first,second,normalize_angle(b->get().world_rotation()-a->get().world_rotation())}});
}
sm::result sm::project::update_constraint(object_id id, constraint_definition definition) {
    auto it = topology_.constraints_.find(id);
    if (it == topology_.constraints_.end()) return result::not_found;
    auto proposed = constraints();
    proposed.at(id).definition_ = std::move(definition);
    auto status = validate_constraints(topology_,proposed);
    if (status == result::success) it->second.definition_ = proposed.at(id).definition_;
    return status;
}
sm::result sm::project::remove_constraint(object_id id) {
    if (!topology_.constraints_.erase(id)) return result::not_found;
    invalidate_object_index();
    return result::success;
}
sm::result sm::project::restore_constraints(const constraint_map& snapshot) {
    auto status = validate_constraints(topology_,snapshot);
    if (status != result::success) return status;
    for (auto& [id,c] : snapshot) if (characters_.contains(id)) return result::duplicate_id;
    topology_.constraints_ = snapshot;
    invalidate_object_index();
    return result::success;
}
