#include "drag_state.h"
#include "../../core/sm_node.h"
#include "../../core/sm_bone.h"
#include "../../core/sm_visit.h"

namespace r = std::ranges;
namespace rv = std::ranges::views;

/*------------------------------------------------------------------------------------------------*/

namespace {

    ui::tool::node_locs node_locations(sm::node& src) {
        std::unordered_map<mdl::handle, sm::point, mdl::handle_hash> locs;

        sm::visit_bone_hierarchy(src,
            [&](sm::maybe_bone_ref prev, sm::bone& bone)->sm::visit_result {
                locs[mdl::to_handle(bone.child_node())] = bone.child_node().world_pos();
                locs[mdl::to_handle(bone.parent_node())] = bone.parent_node().world_pos();
                return sm::visit_result::continue_traversal;
            }
        );

        return locs |
            rv::transform(
                [](auto&& mv)->std::tuple<mdl::handle, sm::point> {
                    const auto& [hnd, pt] = mv;
                    return { hnd,pt };
                }
        ) | r::to<std::vector>();
    }
}

/*------------------------------------------------------------------------------------------------*/

ui::tool::rotation_state::rotation_state( 
        sm::node_ref axis, 
        sm::node_ref rotating, 
        sm::bone_ref bone, 
        ui::tool::sel_drag_mode mode) :

            axis_(axis),
            rotating_(rotating),
            bone_(bone),
            initial_theta_(
                sm::normalize_angle(
                    sm::angle_from_u_to_v(axis->world_pos(), rotating->world_pos())
                )
            ),
            mode_(mode),
            old_locs_(std::make_unique<node_locs>(node_locations(axis))),
            radius_(sm::distance(axis_->world_pos(), rotating_->world_pos())) {
}

const sm::node& ui::tool::rotation_state::axis() const {
    return axis_.get();
}

const sm::node& ui::tool::rotation_state::rotating() const {
    return rotating_.get();
}

const sm::bone& ui::tool::rotation_state::bone() const {
    return bone_.get();
}

sm::node& ui::tool::rotation_state::axis() {
    return axis_.get();
}

sm::node& ui::tool::rotation_state::rotating() {
    return rotating_.get();
}

sm::bone& ui::tool::rotation_state::bone() {
    return bone_.get();
}

double ui::tool::rotation_state::initial_theta() const {
    return initial_theta_;
}

const ui::tool::node_locs& ui::tool::rotation_state::old_node_locs() const {
    return *old_locs_;
}

ui::tool::node_locs ui::tool::rotation_state::current_node_locs() const {
    return node_locations(axis_);
}

ui::tool::sel_drag_mode ui::tool::rotation_state::mode() const {
    return mode_;
}

double ui::tool::rotation_state::radius() const {
    return radius_;
}
