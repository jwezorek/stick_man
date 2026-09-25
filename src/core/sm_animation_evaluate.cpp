#include "sm_animation.hpp"
#include "sm_geometry_batch.hpp"
#include "sm_skeleton.hpp"
#include "sm_ik.hpp"
#include "sm_visit.hpp"
#include <algorithm>
#include <cmath>
#include <map>
#include <stdexcept>
#include <unordered_set>

namespace {
    bool valid_pivot(sm::rotation_pivot pivot) {
        return pivot == sm::rotation_pivot::root || pivot == sm::rotation_pivot::tip;
    }
    bool valid_propagation(sm::rotation_propagation propagation) {
        return propagation == sm::rotation_propagation::hierarchy ||
            propagation == sm::rotation_propagation::bone_only;
    }
    bool valid_reference(sm::translation_reference reference) {
        return reference==sm::translation_reference::animation_root ||
            reference==sm::translation_reference::character_root ||
            reference==sm::translation_reference::bone;
    }

    std::vector<const sm::animation_action*> ordinary_action_order(const sm::animation& animation) {
        std::vector<const sm::animation_action*> ordered;
        for (const auto& layer : animation.layers) {
            std::vector<const sm::animation_action*> actions;
            actions.reserve(layer.actions.size());
            for (const auto& action : layer.actions) actions.push_back(&action);
            std::ranges::stable_sort(actions, {}, [](auto action) { return action->start; });
            ordered.insert(ordered.end(), actions.begin(), actions.end());
        }
        return ordered;
    }

    double absolute_progress(const sm::animation_action& action, sm::animation_time time) {
        if (time <= action.start) return 0.0;
        if (time >= action.start + action.duration) return 1.0;
        return double(time - action.start) / double(action.duration);
    }

    using node_pose = std::unordered_map<sm::object_id, sm::point>;

    node_pose capture_node_pose(const sm::topology& topology) {
        node_pose result;
        for (auto skeleton : topology.skeletons())
            for (auto node : skeleton->nodes()) result.emplace(node->id(), node->world_pos());
        return result;
    }

    void restore_node_pose(const node_pose& pose, const sm::topology& topology) {
        sm::geometry_batch batch(topology);
        for (const auto& [id, pt] : pose)
            if (auto node = topology.get<sm::node>(id)) node->get().set_world_pos(pt);
        if (batch.commit() != sm::result::success) throw std::invalid_argument("cached pose violates rigid constraints");
    }

    bool equal_constraints(const sm::constraint_map& a, const sm::constraint_map& b) {
        if (a.size() != b.size()) return false;
        auto right = b.begin();
        for (const auto& [id, left] : a) {
            const auto& other = right->second;
            if (id != right->first || left.name() != other.name() ||
                    left.definition().index() != other.definition().index()) return false;
            if (auto rotation = left.rotation()) {
                auto rhs = other.rotation();
                if (rotation->target_bone != rhs->target_bone || rotation->reference != rhs->reference ||
                        rotation->allowed.start_angle != rhs->allowed.start_angle ||
                        rotation->allowed.span_angle != rhs->allowed.span_angle) return false;
            } else {
                auto triangle = left.triangle();
                auto rhs = other.triangle();
                if (triangle->first_bone != rhs->first_bone || triangle->second_bone != rhs->second_bone ||
                        triangle->relative_angle != rhs->relative_angle) return false;
            }
            ++right;
        }
        return true;
    }

    struct bone_semantics {
        sm::object_id id;
        sm::object_id skeleton;
        sm::object_id parent;
        sm::object_id child;
        double length = 0.0;
    };

    bool operator==(const bone_semantics& a, const bone_semantics& b) {
        return a.id == b.id && a.skeleton == b.skeleton && a.parent == b.parent &&
            a.child == b.child && a.length == b.length;
    }

    std::vector<bone_semantics> capture_bone_semantics(const sm::topology& topology) {
        std::vector<bone_semantics> result;
        for (auto skeleton : topology.skeletons()) for (auto bone : skeleton->bones())
            result.push_back({bone->id(),skeleton->id(),bone->parent_node().id(),bone->child_node().id(),
                bone->length()});
        std::ranges::sort(result, {}, &bone_semantics::id);
        return result;
    }

    bool equal_path_geometry(const sm::motion_path_geometry& a, const sm::motion_path_geometry& b) {
        if (a.index() != b.index()) return false;
        return std::visit(sm::overloaded{
            [](const sm::line_path& x, const sm::line_path& y) {
                return x.start == y.start && x.end == y.end;
            },
            [](const sm::cubic_bezier_path& x, const sm::cubic_bezier_path& y) {
                return x.start == y.start && x.control1 == y.control1 &&
                    x.control2 == y.control2 && x.end == y.end;
            },
            [](const sm::spline_path& x, const sm::spline_path& y) {
                if (x.segments.size() != y.segments.size()) return false;
                for (std::size_t i=0;i<x.segments.size();++i) {
                    const auto& a=x.segments[i]; const auto& b=y.segments[i];
                    if (!(a.start==b.start && a.control1==b.control1 &&
                          a.control2==b.control2 && a.end==b.end)) return false;
                }
                return true;
            },
            [](const auto&, const auto&) { return false; }
        }, a, b);
    }

    bool equal_motion_path(const sm::motion_path& a, const sm::motion_path& b) {
        return equal_path_geometry(a.geometry(),b.geometry());
    }

    bool equal_action_data(const sm::action_data& a, const sm::action_data& b) {
        if (a.index() != b.index()) return false;
        return std::visit(sm::overloaded{
            [](const sm::rigid_rotation& x, const sm::rigid_rotation& y) {
                return x.bone==y.bone && x.pivot==y.pivot && x.angle==y.angle && x.propagation==y.propagation;
            },
            [](const sm::ik_rotation& x, const sm::ik_rotation& y) {
                return x.effector==y.effector && x.pivot_node==y.pivot_node && x.angle==y.angle;
            },
            [](const sm::rigid_translation& x, const sm::rigid_translation& y) {
                return x.skeletons==y.skeletons && equal_motion_path(x.path,y.path) &&
                    x.reference==y.reference && x.reference_bone==y.reference_bone;
            },
            [](const sm::ik_translation& x, const sm::ik_translation& y) {
                return x.effector==y.effector && x.pins==y.pins && equal_motion_path(x.path,y.path) &&
                    x.reference==y.reference && x.reference_bone==y.reference_bone;
            },
            [](const auto&, const auto&) { return false; }
        }, a, b);
    }

    bool equal_action(const sm::animation_action& a, const sm::animation_action& b) {
        return a.id==b.id && a.start==b.start && a.duration==b.duration && a.easing==b.easing &&
            equal_action_data(a.data,b.data);
    }

    std::vector<double> ik_region_bone_lengths(sm::node& effector, const std::vector<sm::node_ref>& pins) {
        std::unordered_set<sm::object_id> boundaries;
        boundaries.reserve(pins.size());
        for (auto pin : pins) boundaries.insert(pin->id());
        std::vector<double> lengths;
        sm::visit_nodes_and_bones(effector,
            [&](sm::node& node) {
                if (&node != &effector && boundaries.contains(node.id()))
                    return sm::visit_result::terminate_branch;
                return sm::visit_result::continue_traversal;
            },
            [&](sm::bone& bone) {
                const double length=bone.scaled_length();
                if (length>0.0 && std::isfinite(length)) lengths.push_back(length);
                return sm::visit_result::continue_traversal;
            });
        return lengths;
    }

    std::optional<double> ik_continuation_step(sm::node& effector, const std::vector<sm::node_ref>& pins) {
        auto lengths=ik_region_bone_lengths(effector,pins);
        if(lengths.empty()) return {};
        std::ranges::sort(lengths);
        const auto n=lengths.size();
        const double median=n%2 ? lengths[n/2] : (lengths[n/2-1]+lengths[n/2])*0.5;
        const double step=median*sm::ik_continuation_step_bone_fraction;
        if(!(step>0.0) || !std::isfinite(step)) return {};
        return step;
    }

    struct canonical_distance {
        std::size_t full_steps = 0;
        bool exact_boundary = false;
    };

    canonical_distance canonical_steps(double requested_distance, double step) {
        if (!(requested_distance>0.0)) return {};
        const double ratio=requested_distance/step;
        const double nearest=std::round(ratio);
        const double tolerance=1e-12*std::max(1.0,std::abs(ratio));
        if(std::abs(ratio-nearest)<=tolerance && nearest>=0.0)
            return {static_cast<std::size_t>(nearest),true};
        return {static_cast<std::size_t>(std::floor(ratio)),false};
    }
}

struct sm::animation_evaluator::implementation {
    struct ik_cache_entry {
        animation_action action;
        object_id character_root_bone;
        node_pose base_pose;
        node_pose incoming_pose;
        std::vector<bone_semantics> topology_semantics;
        constraint_map constraints;
        double continuation_step = 0.0;
        std::map<std::size_t,node_pose> checkpoints;
    };

    animation_evaluator_cache_policy policy;
    std::unordered_map<object_id,ik_cache_entry> ik_cache;

    explicit implementation(animation_evaluator_cache_policy p) : policy(p) {
        if(policy.continuation_steps_per_checkpoint==0) policy.continuation_steps_per_checkpoint=1;
    }

    ik_cache_entry& cache_for(const animation_action& action, const pose& base,
            object_id character_root_bone, const topology& working, double continuation_step) {
        const auto incoming=capture_node_pose(working);
        const auto semantics=capture_bone_semantics(working);
        auto found=ik_cache.find(action.id);
        const bool reusable=found!=ik_cache.end() && equal_action(found->second.action,action) &&
            found->second.character_root_bone==character_root_bone &&
            found->second.base_pose==base.node_positions && found->second.incoming_pose==incoming &&
            found->second.topology_semantics==semantics &&
            equal_constraints(found->second.constraints,working.constraints()) &&
            found->second.continuation_step==continuation_step;
        if(!reusable) {
            ik_cache_entry fresh;
            fresh.action=action;
            fresh.character_root_bone=character_root_bone;
            fresh.base_pose=base.node_positions;
            fresh.incoming_pose=incoming;
            fresh.topology_semantics=semantics;
            fresh.constraints=working.constraints();
            fresh.continuation_step=continuation_step;
            fresh.checkpoints.emplace(0,incoming);
            if(found==ik_cache.end()) found=ik_cache.emplace(action.id,std::move(fresh)).first;
            else found->second=std::move(fresh);
        }
        return found->second;
    }

    template<typename TargetAtDistance, typename SolveTarget>
    void continue_ik(ik_cache_entry& cache, topology& working, double requested_distance,
            TargetAtDistance&& target_at_distance, SolveTarget&& solve_target) {
        if(!(requested_distance>0.0)) return;
        const auto sequence=canonical_steps(requested_distance,cache.continuation_step);
        auto checkpoint=cache.checkpoints.upper_bound(sequence.full_steps);
        if(checkpoint==cache.checkpoints.begin()) checkpoint=cache.checkpoints.begin();
        else --checkpoint;
        restore_node_pose(checkpoint->second,working);

        for(std::size_t step_index=checkpoint->first+1;step_index<=sequence.full_steps;++step_index) {
            solve_target(target_at_distance(cache.continuation_step*double(step_index)));
            if(step_index%policy.continuation_steps_per_checkpoint==0)
                cache.checkpoints.try_emplace(step_index,capture_node_pose(working));
        }
        if(!sequence.exact_boundary) solve_target(target_at_distance(requested_distance));
    }
};

sm::animation_evaluator::animation_evaluator(animation_evaluator_cache_policy policy) :
    implementation_(std::make_unique<implementation>(policy)) {}
sm::animation_evaluator::~animation_evaluator()=default;
sm::animation_evaluator::animation_evaluator(animation_evaluator&&) noexcept=default;
sm::animation_evaluator& sm::animation_evaluator::operator=(animation_evaluator&&) noexcept=default;
void sm::animation_evaluator::invalidate() { implementation_->ik_cache.clear(); }

sm::point sm::reference_frame::vector_to_world(point local) const {
    const double c=std::cos(angle),s=std::sin(angle);
    return {c*local.x-s*local.y,s*local.x+c*local.y};
}

sm::point sm::reference_frame::vector_to_local(point world) const {
    const double c=std::cos(angle),s=std::sin(angle);
    return {c*world.x+s*world.y,-s*world.x+c*world.y};
}

sm::point sm::reference_frame::local_to_world(point local) const {
    return origin+vector_to_world(local);
}

sm::point sm::reference_frame::world_to_local(point world) const {
    return vector_to_local(world-origin);
}

std::optional<sm::reference_frame> sm::bone_reference_frame(object_id bone_id,const topology& topology) {
    auto bone=topology.get<sm::bone>(bone_id); if(!bone) return {};
    const auto origin=bone->get().parent_node().world_pos();
    const auto tip=bone->get().child_node().world_pos();
    return reference_frame{origin,sm::angle_from_u_to_v(origin,tip)};
}

std::optional<sm::reference_frame> sm::translation_reference_frame(translation_reference reference,
        object_id reference_bone,object_id character_root_bone,const pose& base,const topology& working) {
    if(!valid_reference(reference)) return {};
    if(reference==translation_reference::animation_root) {
        auto bone=working.get<sm::bone>(character_root_bone); if(!bone) return {};
        const auto u=base.node_positions.find(bone->get().parent_node().id());
        const auto v=base.node_positions.find(bone->get().child_node().id());
        if(u==base.node_positions.end() || v==base.node_positions.end()) return {};
        return reference_frame{u->second,sm::angle_from_u_to_v(u->second,v->second)};
    }
    return bone_reference_frame(reference==translation_reference::character_root ? character_root_bone : reference_bone,working);
}

std::vector<sm::object_id> sm::animation_evaluation_order(const animation& animation) {
    animation.duration();
    auto ordered = ordinary_action_order(animation);
    std::vector<object_id> ids;
    ids.reserve(ordered.size());
    for (const auto* action : ordered) ids.push_back(action->id);
    return ids;
}

sm::animation_evaluation sm::animation_evaluator::evaluate(const animation& animation, const pose& base,
        object_id character_root_bone, topology& working, animation_time time) {
    animation.duration(); // Validate intervals before touching the working pose.
    for (auto s : working.skeletons()) for (auto n : s->nodes())
        if (!base.node_positions.contains(n->id())) throw std::invalid_argument("Incomplete animation base pose");

    const auto ordered = ordinary_action_order(animation);
    apply_pose(base, working);
    animation_evaluation report;
    report.evaluation_order.reserve(ordered.size());

    for (const auto* action : ordered) {
        report.evaluation_order.push_back(action->id);
        auto& context = report.contexts[action->id];
        const double progress = absolute_progress(*action, time);
        const double eased = ease(action->easing, progress);

        std::visit(sm::overloaded{
            [&](const rigid_rotation& rotation) {
                auto bone = working.get<sm::bone>(rotation.bone);
                if (!bone || !std::isfinite(rotation.angle) || !valid_pivot(rotation.pivot) ||
                    !valid_propagation(rotation.propagation)) {
                    report.invalid_actions.push_back(action->id);
                    return;
                }
                auto& pivot = rotation.pivot == rotation_pivot::root ?
                    bone->get().parent_node() : bone->get().child_node();
                auto& rotating = rotation.pivot == rotation_pivot::root ?
                    bone->get().child_node() : bone->get().parent_node();
                context.rotation = rotation_evaluation_context{pivot.world_pos(), rotating.world_pos()};
                if (progress > 0.0) bone->get().rotate_by(rotation.angle * eased, pivot,
                    rotation.propagation == rotation_propagation::bone_only);
            },
            [&](const ik_rotation& rotation) {
                auto effector = working.get<sm::node>(rotation.effector);
                auto pivot = working.get<sm::node>(rotation.pivot_node);
                if (!effector || !pivot || effector->get().id() == pivot->get().id() ||
                    &effector->get().owner() != &pivot->get().owner() || !std::isfinite(rotation.angle)) {
                    report.invalid_actions.push_back(action->id);
                    return;
                }
                const auto origin = pivot->get().world_pos();
                const auto effector_pos = effector->get().world_pos();
                context.rotation = rotation_evaluation_context{origin, effector_pos};
                const double radius = sm::distance(origin, effector_pos);
                if (!(radius > 0.0) || !std::isfinite(radius)) {
                    report.invalid_actions.push_back(action->id);
                    return;
                }
                if (progress > 0.0 && rotation.angle != 0.0) {
                    std::vector<sm::node_ref> pins{*pivot};
                    const auto continuation_step=ik_continuation_step(effector->get(),pins);
                    if(!continuation_step) {
                        report.invalid_actions.push_back(action->id);
                        return;
                    }
                    auto& cache=implementation_->cache_for(*action,base,character_root_bone,working,*continuation_step);
                    const double initial_theta=sm::angle_from_u_to_v(origin,effector_pos);
                    const double requested_distance=radius*std::abs(rotation.angle)*eased;
                    const double direction=rotation.angle<0.0 ? -1.0 : 1.0;
                    implementation_->continue_ik(cache,working,requested_distance,
                        [&](double target_distance) {
                            const double theta=initial_theta+direction*(target_distance/radius);
                            return origin+radius*sm::point(std::cos(theta),std::sin(theta));
                        },
                        [&](sm::point target) { sm::perform_ik(*effector,target,*pivot); });
                }
            },
            [&](const rigid_translation& translation) {
                auto frame=translation_reference_frame(translation.reference,translation.reference_bone,character_root_bone,base,working);
                context.translation_reference_frame=frame;
                if(!frame) { report.invalid_actions.push_back(action->id); return; }
                bool valid=!translation.skeletons.empty();
                std::vector<sm::skel_ref> targets;
                for(auto id:translation.skeletons) {
                    auto skeleton=working.skeleton(id);
                    if(!skeleton) { valid=false; break; }
                    targets.push_back(*skeleton);
                }
                if(!valid) { report.invalid_actions.push_back(action->id); return; }
                if (progress > 0.0) {
                    const auto local=translation.path.evaluate_by_arc_length(progress);
                    const auto world_delta=frame->vector_to_world(local);
                    auto matrix=sm::translation_matrix(world_delta);
                    for(auto skeleton:targets) skeleton->apply(matrix);
                }
            },
            [&](const ik_translation& translation) {
                auto effector=working.get<sm::node>(translation.effector);
                auto frame=translation_reference_frame(translation.reference,translation.reference_bone,character_root_bone,base,working);
                context.translation_reference_frame=frame;
                if (effector) context.translation_anchor_world=effector->get().world_pos();
                if(!effector || !frame) { report.invalid_actions.push_back(action->id); return; }
                std::vector<sm::node_ref> pins; bool valid=true;
                pins.reserve(translation.pins.size());
                for(auto id:translation.pins) {
                    auto pin=working.get<sm::node>(id);
                    if(!pin || id==translation.effector || &pin->get().owner()!=&effector->get().owner()) { valid=false; break; }
                    pins.push_back(*pin);
                }
                if(!valid) { report.invalid_actions.push_back(action->id); return; }
                const double path_length=translation.path.length();
                if (progress > 0.0 && path_length > 0.0) {
                    const auto continuation_step=ik_continuation_step(effector->get(),pins);
                    if(!continuation_step) { report.invalid_actions.push_back(action->id); return; }
                    const auto anchor=effector->get().world_pos();
                    auto& cache=implementation_->cache_for(*action,base,character_root_bone,working,*continuation_step);
                    const double requested_distance=path_length*progress;
                    implementation_->continue_ik(cache,working,requested_distance,
                        [&](double target_distance) {
                            const double fraction=target_distance/path_length;
                            return anchor+frame->vector_to_world(translation.path.evaluate_by_arc_length(fraction));
                        },
                        [&](sm::point target) {
                            sm::perform_ik(std::vector<std::tuple<sm::node_ref,sm::point>>{{*effector,target}},pins);
                        });
                }
            }
        }, action->data);
    }
    return report;
}

sm::animation_evaluation sm::evaluate_animation(const animation& animation, const pose& base,
        object_id character_root_bone, topology& working, animation_time time) {
    animation_evaluator evaluator;
    return evaluator.evaluate(animation,base,character_root_bone,working,time);
}
