#include "sm_geometry_batch.hpp"
#include <cmath>
sm::geometry_batch::geometry_batch(const topology& t) : topology_(t), outer_(t.geometry_edit_depth_++ == 0) {
    if (outer_) for (auto s : t.skeletons()) for (auto n : s->nodes())
        saved_.emplace_back(const_cast<node*>(n.ptr()),n->world_pos());
}
sm::geometry_batch::~geometry_batch() {
    if (!committed_) for (auto [n,p] : saved_) n->set_world_pos(p);
    --topology_.geometry_edit_depth_;
}
sm::result sm::geometry_batch::commit() {
    if (outer_) {
        for (auto& [id,c] : topology_.constraints()) if (auto tri = c.triangle()) {
            auto a = topology_.get<bone>(tri->first_bone), b = topology_.get<bone>(tri->second_bone);
            if (!a || !b || std::abs(angular_distance(
                    b->get().world_rotation()-a->get().world_rotation(),tri->relative_angle)) > 1e-8)
                return result::unsatisfiable_constraints;
        }
        for (auto s : topology_.skeletons()) for (auto n : s->nodes())
            if (!std::isfinite(n->world_x()) || !std::isfinite(n->world_y())) return result::out_of_bounds;
    }
    committed_ = true;
    return result::success;
}
