#include "sm_animation.hpp"
#include "sm_skeleton.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <unordered_set>

namespace {
constexpr double epsilon = 1e-8;
template<class... Ts> struct overloaded : Ts... { using Ts::operator()...; };
sm::point lerp(sm::point a, sm::point b, double t) { return (1.0-t)*a + t*b; }
sm::point cubic_at(const sm::cubic_bezier_path& c, double t) {
    const double u=1.0-t, b0=u*u*u, b1=3*u*u*t, b2=3*u*t*t, b3=t*t*t;
    return b0*c.start + b1*c.control1 + b2*c.control2 + b3*c.end;
}

bool finite(sm::point p) { return std::isfinite(p.x) && std::isfinite(p.y); }
bool near_zero(sm::point p) { return sm::distance(p,{0,0}) <= 1e-7; }
bool valid_reference(sm::translation_reference reference) {
    return reference==sm::translation_reference::animation_root ||
        reference==sm::translation_reference::character_root ||
        reference==sm::translation_reference::bone;
}
void validate_cubic(const sm::cubic_bezier_path& c) {
    if(!finite(c.start)||!finite(c.control1)||!finite(c.control2)||!finite(c.end))
        throw std::invalid_argument("Invalid motion path point");
}
void validate_path(const sm::motion_path& path) {
    std::visit([](const auto& p) {
        using T=std::decay_t<decltype(p)>;
        if constexpr(std::is_same_v<T,sm::line_path>) {
            if(!finite(p.start)||!finite(p.end)||!near_zero(p.start)) throw std::invalid_argument("Invalid straight motion path");
        } else if constexpr(std::is_same_v<T,sm::cubic_bezier_path>) {
            validate_cubic(p); if(!near_zero(p.start)) throw std::invalid_argument("Motion path must begin at zero displacement");
        } else {
            if(p.segments.empty()) throw std::invalid_argument("Spline motion path has no segments");
            for(std::size_t i=0;i<p.segments.size();++i) {
                validate_cubic(p.segments[i]);
                if(i==0 && !near_zero(p.segments[i].start)) throw std::invalid_argument("Motion path must begin at zero displacement");
                if(i) {
                    const auto& previous=p.segments[i-1];
                    if(sm::distance(previous.end,p.segments[i].start)>1e-6) throw std::invalid_argument("Disconnected spline motion path");
                    const auto incoming=previous.end-previous.control2;
                    const auto outgoing=p.segments[i].control1-p.segments[i].start;
                    const double in_len=sm::distance(incoming,{0,0}),out_len=sm::distance(outgoing,{0,0});
                    if(in_len>epsilon && out_len>epsilon) {
                        const double cross=incoming.x*outgoing.y-incoming.y*outgoing.x;
                        const double dot=incoming.x*outgoing.x+incoming.y*outgoing.y;
                        if(dot<=0.0 || std::abs(cross)>1e-5*in_len*out_len) throw std::invalid_argument("Spline knot is not smooth");
                    }
                }
            }
        }
    },path.geometry());
}
}

std::vector<sm::animation_dependency> sm::animation_action_dependencies(const animation_action& action) {
    return std::visit(overloaded{
        [](const rigid_rotation& data) {
            return std::vector<animation_dependency>{{animation_dependency_kind::bone, data.bone}};
        },
        [](const ik_rotation& data) {
            return std::vector<animation_dependency>{
                {animation_dependency_kind::node, data.effector},
                {animation_dependency_kind::node, data.pivot_node}};
        },
        [](const rigid_translation& data) {
            std::vector<animation_dependency> dependencies;
            dependencies.reserve(data.skeletons.size() + 1);
            for (const auto id : data.skeletons)
                dependencies.push_back({animation_dependency_kind::skeleton, id});
            if (data.reference == translation_reference::bone)
                dependencies.push_back({animation_dependency_kind::bone, data.reference_bone});
            return dependencies;
        },
        [](const ik_translation& data) {
            std::vector<animation_dependency> dependencies;
            dependencies.reserve(data.pins.size() + 2);
            dependencies.push_back({animation_dependency_kind::node, data.effector});
            for (const auto id : data.pins)
                dependencies.push_back({animation_dependency_kind::node, id});
            if (data.reference == translation_reference::bone)
                dependencies.push_back({animation_dependency_kind::bone, data.reference_bone});
            return dependencies;
        }
    }, action.data);
}

void sm::motion_path::set_geometry(motion_path_geometry path) {
    geometry_=std::move(path); arc_cache_.clear(); total_length_=0.0;
}
sm::motion_path_kind sm::motion_path::kind() const noexcept {
    if(std::holds_alternative<line_path>(geometry_)) return motion_path_kind::straight;
    if(std::holds_alternative<cubic_bezier_path>(geometry_)) return motion_path_kind::curve;
    return motion_path_kind::spline;
}
sm::point sm::motion_path::final_displacement() const {
    return std::visit([](const auto& p)->point {
        using T=std::decay_t<decltype(p)>;
        if constexpr(std::is_same_v<T,line_path>||std::is_same_v<T,cubic_bezier_path>) return p.end;
        else return p.segments.empty()?point{}:p.segments.back().end;
    },geometry_);
}
void sm::motion_path::rebuild_arc_cache() const {
    arc_cache_.clear(); total_length_=0.0;
    constexpr int samples_per_cubic=64;
    auto append=[&](std::size_t segment, auto evaluator, int samples) {
        point previous=evaluator(0.0);
        // Keep a zero-parameter sample for every spline segment. Adjacent segments
        // share the same cumulative distance at their knot; the duplicate lets a
        // lookup just past the knot interpolate within the new segment instead of
        // crossing two unrelated Bezier parameter spaces.
        arc_cache_.push_back({segment,0.0,total_length_});
        for(int i=1;i<=samples;++i) {
            const double u=double(i)/samples; const point current=evaluator(u);
            total_length_ += distance(previous,current);
            arc_cache_.push_back({segment,u,total_length_}); previous=current;
        }
    };
    std::visit([&](const auto& p) {
        using T=std::decay_t<decltype(p)>;
        if constexpr(std::is_same_v<T,line_path>) append(0,[&](double u){return lerp(p.start,p.end,u);},1);
        else if constexpr(std::is_same_v<T,cubic_bezier_path>) append(0,[&](double u){return cubic_at(p,u);},samples_per_cubic);
        else for(std::size_t i=0;i<p.segments.size();++i) append(i,[&](double u){return cubic_at(p.segments[i],u);},samples_per_cubic);
    },geometry_);
}
double sm::motion_path::length() const { if(arc_cache_.empty()) rebuild_arc_cache(); return total_length_; }
sm::point sm::motion_path::evaluate_by_arc_length(double distance_fraction) const {
    distance_fraction=std::clamp(distance_fraction,0.0,1.0);
    if(arc_cache_.empty()) rebuild_arc_cache();
    if(arc_cache_.empty()||total_length_<=epsilon) return final_displacement()*distance_fraction;
    const double target=distance_fraction*total_length_;
    auto hi=std::lower_bound(arc_cache_.begin(),arc_cache_.end(),target,
        [](const arc_sample& sample,double value){return sample.cumulative<value;});
    if(hi==arc_cache_.begin()) hi=std::next(arc_cache_.begin());
    if(hi==arc_cache_.end()) return final_displacement();
    auto lo=std::prev(hi);
    // A target exactly at a spline boundary can straddle segment records. Prefer the
    // later segment's zero parameter so interpolation never crosses parameter spaces.
    if(lo->segment!=hi->segment) return std::visit([&](const auto& p)->point {
        using T=std::decay_t<decltype(p)>;
        if constexpr(std::is_same_v<T,spline_path>) return p.segments[hi->segment].start;
        else return final_displacement();
    },geometry_);
    const double span=hi->cumulative-lo->cumulative;
    const double f=span>epsilon?(target-lo->cumulative)/span:0.0;
    const double u=lo->parameter+f*(hi->parameter-lo->parameter);
    return std::visit([&](const auto& p)->point {
        using T=std::decay_t<decltype(p)>;
        if constexpr(std::is_same_v<T,line_path>) return lerp(p.start,p.end,u);
        else if constexpr(std::is_same_v<T,cubic_bezier_path>) return cubic_at(p,u);
        else return p.segments[lo->segment].end==p.segments[lo->segment].start ? p.segments[lo->segment].end : cubic_at(p.segments[lo->segment],u);
    },geometry_);
}

double sm::ease(easing curve, double u) {
    u = std::clamp(u, 0.0, 1.0);
    switch (curve) {
    case easing::ease_in: return u * u;
    case easing::ease_out: return u * (2 - u);
    case easing::ease_in_out: return u < .5 ? 2*u*u : 1 - 2*(1-u)*(1-u);
    case easing::smoothstep: return u*u*(3-2*u);
    default: return u;
    }
}
sm::animation_time sm::animation::duration() const {
    animation_time end = 0;
    for (const auto& layer : layers) for (const auto& a : layer.actions) {
        if (a.start < 0 || a.duration <= 0 || a.start > std::numeric_limits<animation_time>::max() - a.duration)
            throw std::invalid_argument("Invalid action interval");
        end = std::max(end, a.start + a.duration);
    }
    return end;
}
const sm::pose* sm::animation_assets::find_pose(object_id id) const {
    for (const auto& p : poses) if (p.id == id) return &p;
    return nullptr;
}
const sm::animation* sm::animation_assets::find_animation(object_id id) const {
    for (const auto& a : animations) if (a.id == id) return &a;
    return nullptr;
}
void sm::animation_assets::validate() const {
    if (!find_pose(default_pose)) throw std::invalid_argument("Missing Default pose");
    std::unordered_set<object_id> ids;
    auto add = [&](object_id id) { if (id.is_nil() || !ids.insert(id).second) throw std::invalid_argument("Duplicate animation asset ID"); };
    for (const auto& p : poses) {
        add(p.id);
        for (const auto& [id, pt] : p.node_positions)
            if (id.is_nil() || !std::isfinite(pt.x) || !std::isfinite(pt.y)) throw std::invalid_argument("Invalid pose position");
    }
    for (const auto& a : animations) {
        add(a.id);
        if (!find_pose(a.base_pose)) throw std::invalid_argument("Missing animation base pose");
        a.duration();
        for (const auto& layer : a.layers) {
            std::vector<const animation_action*> sorted;
            for (const auto& action : layer.actions) {
                add(action.id);
                std::visit([](const auto& data) {
                    using T = std::decay_t<decltype(data)>;
                    if constexpr (std::is_same_v<T, rigid_rotation>) {
                        if (data.bone.is_nil() || !std::isfinite(data.angle) ||
                            (data.pivot != rotation_pivot::root && data.pivot != rotation_pivot::tip) ||
                            (data.propagation != rotation_propagation::hierarchy && data.propagation != rotation_propagation::bone_only))
                            throw std::invalid_argument("Invalid bone rotation action");
                    } else if constexpr (std::is_same_v<T, ik_rotation>) {
                        if (data.effector.is_nil() || data.pivot_node.is_nil() || data.effector == data.pivot_node || !std::isfinite(data.angle))
                            throw std::invalid_argument("Invalid IK rotation action");
                    } else if constexpr(std::is_same_v<T,rigid_translation>) {
                        if(!valid_reference(data.reference)) throw std::invalid_argument("Invalid translation reference");
                        if(data.skeletons.empty()) throw std::invalid_argument("Rigid translation has no target skeletons");
                        std::unordered_set<object_id> targets;
                        for(auto id:data.skeletons) if(id.is_nil() || !targets.insert(id).second) throw std::invalid_argument("Invalid rigid translation target");
                        if(data.reference==translation_reference::bone && data.reference_bone.is_nil()) throw std::invalid_argument("Missing translation reference bone");
                        validate_path(data.path);
                    } else if constexpr(std::is_same_v<T,ik_translation>) {
                        if(!valid_reference(data.reference)) throw std::invalid_argument("Invalid translation reference");
                        if(data.effector.is_nil()) throw std::invalid_argument("Invalid IK translation action");
                        std::unordered_set<object_id> pins;
                        for(auto id:data.pins) if(id.is_nil() || id==data.effector || !pins.insert(id).second) throw std::invalid_argument("Invalid IK translation pin");
                        if(data.reference==translation_reference::bone && data.reference_bone.is_nil()) throw std::invalid_argument("Missing translation reference bone");
                        validate_path(data.path);
                    }
                }, action.data);
                sorted.push_back(&action);
            }
            std::ranges::sort(sorted, {}, [](auto a) { return a->start; });
            for (std::size_t i = 1; i < sorted.size(); ++i)
                if (sorted[i-1]->start + sorted[i-1]->duration > sorted[i]->start)
                    throw std::invalid_argument("Actions overlap within a layer");
        }
    }
}
void sm::animation_assets::validate(const topology& topology, object_id character_root_bone) const {
    validate();
    for (const auto& animation : animations) {
        for (const auto& layer : animation.layers) for (const auto& action : layer.actions) {
            for (const auto& dependency : animation_action_dependencies(action)) {
                const bool resolved = [&] {
                    switch (dependency.kind) {
                    case animation_dependency_kind::node:
                        return topology.get<sm::node>(dependency.id).has_value();
                    case animation_dependency_kind::bone:
                        return topology.get<sm::bone>(dependency.id).has_value();
                    case animation_dependency_kind::skeleton:
                        return topology.skeleton(dependency.id).has_value();
                    }
                    return false;
                }();
                if (!resolved) throw std::invalid_argument("Animation action contains an unresolved project reference");
            }
        }
        validate_animation_order(animation, character_root_bone, topology);
    }
}
sm::pose sm::capture_pose(const topology& topology, const std::vector<object_id>& skeletons, std::string name) {
    pose p; p.name = std::move(name);
    for (const auto& id : skeletons) if (auto s = topology.skeleton(id))
        for (auto n : s->get().nodes()) p.node_positions.emplace(n->id(), n->world_pos());
    return p;
}
void sm::initialize_animation_assets(animation_assets& assets, const topology& topology, const std::vector<object_id>& skeletons) {
    if (!assets.poses.empty() || skeletons.empty()) return;
    auto p = capture_pose(topology, skeletons, "Default");
    assets.default_pose = p.id;
    assets.poses.push_back(std::move(p));
}
void sm::reconcile_animation_poses(animation_assets& assets, const topology& topology,
        const std::vector<object_id>& skeletons) {
    initialize_animation_assets(assets, topology, skeletons);
    if (assets.poses.empty()) return;

    std::unordered_map<object_id, point> members;
    for (const auto& sid : skeletons) if (auto skel = topology.skeleton(sid))
        for (auto node : skel->get().nodes()) members.emplace(node->id(), node->world_pos());

    for (auto& p : assets.poses) {
        if (p.id == assets.default_pose) {
            std::erase_if(p.node_positions, [&](const auto& entry) { return !members.contains(entry.first); });
            for (const auto& [id, pt] : members) p.node_positions.try_emplace(id, pt);
        } else {
            std::erase_if(p.node_positions, [&](const auto& entry) {
                return !topology.get<sm::node>(entry.first).has_value();
            });
        }
    }
}
void sm::remap_animation_assets(animation_assets& assets,
        const std::unordered_map<object_id, object_id>& id_remap) {
    auto remap = [&](object_id& id) {
        if (auto it = id_remap.find(id); it != id_remap.end()) id = it->second;
    };

    for (auto& p : assets.poses) {
        std::unordered_map<object_id, point> remapped;
        remapped.reserve(p.node_positions.size());
        for (const auto& [old_id, pt] : p.node_positions) {
            auto id = old_id;
            remap(id);
            if (!remapped.emplace(id, pt).second)
                throw std::invalid_argument("Animation pose remap produced duplicate node IDs");
        }
        p.node_positions = std::move(remapped);
    }

    for (auto& animation : assets.animations) for (auto& layer : animation.layers)
        for (auto& action : layer.actions) std::visit([&](auto& data) {
            using T = std::decay_t<decltype(data)>;
            if constexpr (std::is_same_v<T, rigid_rotation>) {
                remap(data.bone);
            } else if constexpr (std::is_same_v<T, ik_rotation>) {
                remap(data.effector);
                remap(data.pivot_node);
            } else if constexpr (std::is_same_v<T, rigid_translation>) {
                for (auto& id : data.skeletons) remap(id);
                remap(data.reference_bone);
            } else if constexpr (std::is_same_v<T, ik_translation>) {
                remap(data.effector);
                for (auto& id : data.pins) remap(id);
                remap(data.reference_bone);
            }
        }, action.data);
}
void sm::apply_pose(const pose& pose, const topology& topology) {
    for (const auto& [id, pt] : pose.node_positions) if (auto node = topology.get<sm::node>(id)) node->get().set_world_pos(pt);
}
bool sm::pose_compatible(const pose& pose, const topology& topology, const std::vector<object_id>& skeletons) {
    std::size_t count = 0;
    for (const auto& id : skeletons) if (auto s = topology.skeleton(id)) for (auto n : s->get().nodes()) {
        ++count;
        if (!pose.node_positions.contains(n->id())) return false;
    }
    return count == pose.node_positions.size();
}
