#include "core/sm_animation.hpp"
#include "core/sm_project.hpp"
#include "core/sm_constraint_geometry.hpp"
#include "json.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <numbers>
#include <stdexcept>

namespace {
constexpr double pi = std::numbers::pi;
void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
void near(double a, double b, double tolerance = 1e-7) {
    require(std::abs(a - b) <= tolerance, "scalar mismatch");
}
void same_pose(const sm::skeletal_pose& a, const sm::skeletal_pose& b) {
    require(a.root_positions == b.root_positions, "roots changed");
    require(a.bone_rotations == b.bone_rotations, "rotations changed");
}
struct fixture {
    sm::project project;
    std::vector<sm::object_id> rig;
    sm::node_ref node(sm::point p) { return project.create_skeleton(p).root_node(); }
    sm::bone_ref link(sm::node_ref a, sm::node_ref b) {
        return project.create_bone("bone", a, b).value();
    }
    void scope() {
        rig.clear();
        for (auto s : project.topology().skeletons())
            rig.push_back(s->id());
        std::ranges::sort(rig);
    }
    sm::animation sequence() {
        scope();
        sm::animation a;
        a.name = "constrained fixture";
        a.keyframes.resize(3);
        for (auto& key : a.keyframes)
            key.pose = sm::capture_skeletal_pose(project.topology(), rig);
        a.reconcile_transitions();
        a.transitions[0].duration_seconds = 2;
        a.transitions[1].duration_seconds = 3;
        return a;
    }
    void limit(sm::bone_ref b, sm::rotation_reference reference, sm::angle_range allowed) {
        require(project.add_rotation_constraint(b->id(), reference, allowed).has_value(), "add limit");
    }
    auto sample(const sm::animation& a, double t) {
        const auto authored = a;
        const auto geometry = project.topology().to_json();
        std::map<sm::object_id, std::tuple<double, double, double>> lengths;
        for (auto s : project.topology().skeletons())
            for (auto b : s->bones())
                lengths.emplace(b->id(), std::tuple{ b->length(), b->scaled_length(), b->scale() });
        auto result = sm::sample_constrained_pose(a, t, project.topology(), rig);
        require(project.topology().to_json() == geometry, "sampling mutated topology/constraints");
        for (auto s : project.topology().skeletons())
            for (auto b : s->bones())
                require(lengths.at(b->id()) ==
                        std::tuple{ b->length(), b->scaled_length(), b->scale() },
                    "length/scale mutation");
        require(a.id == authored.id && a.name == authored.name && a.keyframes.size() == authored.keyframes.size() &&
            a.transitions.size() == authored.transitions.size(), "animation mutation");
        for (std::size_t i = 0; i < a.keyframes.size(); ++i) {
            require(a.keyframes[i].id == authored.keyframes[i].id &&
                a.keyframes[i].name == authored.keyframes[i].name, "key identity/name mutation");
            same_pose(a.keyframes[i].pose, authored.keyframes[i].pose);
        }
        for (std::size_t i = 0; i < a.transitions.size(); ++i)
            require(a.transitions[i].id == authored.transitions[i].id &&
                a.transitions[i].duration_seconds == authored.transitions[i].duration_seconds &&
                a.transitions[i].pinned_nodes == authored.transitions[i].pinned_nodes &&
                sm::constraints_to_json(a.transitions[i].rotation_constraints) ==
                    sm::constraints_to_json(authored.transitions[i].rotation_constraints),
                "transition mutation");
        return result;
    }
    // Independent authority: apply only returned successful poses, and validate
    // with Core geometry, not the numeric projection's constraint rows.
    void feasible(const sm::skeletal_pose& pose) {
        sm::topology copy;
        for (auto id : rig)
            require(project.topology().skeleton(id)->get().copy_to(copy).has_value(), "copy");
        sm::constraint_geometry geometry(copy);
        sm::apply_skeletal_pose(pose, copy, rig);
        require(geometry.validate() == sm::result::success, "returned pose infeasible");
        for (auto id : rig)
            for (auto b : project.topology().skeleton(id)->get().bones())
                near(copy.get<sm::bone>(b->id())->get().scaled_length(), b->scaled_length());
    }
};
const sm::constrained_pose_sample& success(const sm::constrained_pose_result& result) {
    require(result.has_value() && result->has_value(), "expected constrained sample");
    return **result;
}
void failed(const sm::constrained_pose_result& result, sm::result error) {
    require(!result.has_value() && result.error() == error, "incorrect failure status");
}
void timing_and_exactness() {
    fixture f;
    auto root = f.node({3, 4}), tip = f.node({13, 4});
    auto bone = f.link(root, tip);
    auto a = f.sequence();
    a.keyframes[0].pose.bone_rotations.at(bone->id()) = 8 * pi;
    a.keyframes[1].pose.bone_rotations.at(bone->id()) = 0.3;
    a.keyframes[2].pose.bone_rotations.at(bone->id()) = 0.6;
    const auto before = f.project.topology().to_json();
    for (double t : {-100., 0., 2., 5., 100.}) {
        auto expected = sm::sample_reference_pose(a, t);
        auto result = f.sample(a, t);
        same_pose(success(result).pose, expected->pose);
        require(std::get<sm::reference_keyframe>(success(result).location).keyframe_id ==
            std::get<sm::reference_keyframe>(expected->location).keyframe_id, "key identity");
    }
    for (double t : {1., 3.5, std::nextafter(2., 0.)}) {
        auto result = f.sample(a, t);
        auto expected = sm::sample_reference_pose(a, t);
        same_pose(success(result).pose, expected->pose);
        const auto& actual = std::get<sm::reference_transition>(success(result).location);
        const auto& wanted = std::get<sm::reference_transition>(expected->location);
        require(actual.transition_id == wanted.transition_id && actual.from_keyframe_id == wanted.from_keyframe_id &&
            actual.to_keyframe_id == wanted.to_keyframe_id && actual.progress == wanted.progress, "timing metadata");
    }
    auto one = a;
    one.keyframes.resize(1);
    one.transitions.clear();
    for (double t : { -100., 0., 100. })
        same_pose(success(f.sample(one, t)).pose, one.keyframes[0].pose);
    sm::animation empty;
    require(f.sample(empty, 1).has_value() && !*f.sample(empty, 1), "empty outcome");
    for (double t : {std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()}) {
        failed(f.sample(a, t), sm::result::invalid_animation);
        failed(f.sample(empty, t), sm::result::invalid_animation);
    }
    auto malformed = a;
    malformed.transitions[0].duration_seconds = 0;
    failed(f.sample(malformed, 0), sm::result::invalid_animation);
    malformed = a;
    malformed.keyframes[1].pose.bone_rotations.clear();
    failed(f.sample(malformed, 0), sm::result::invalid_animation);
    require(f.project.topology().to_json() == before, "timing mutated topology");
}
void transition_pins_govern_sampling_and_reporting() {
    fixture f;
    auto root = f.node({0, 0}), knee = f.node({10, 0}), foot = f.node({20, 0});
    auto upper = f.link(root, knee), lower = f.link(knee, foot);
    auto a = f.sequence();
    const auto rid = root->id(), fid = foot->id();

    a.transitions[0].pinned_nodes.insert(fid);
    a.transitions[1].pinned_nodes.insert(rid);
    a.keyframes[1].pose.root_positions[rid] = {10, -10};
    a.keyframes[1].pose.bone_rotations[upper->id()] = std::numbers::pi / 2;
    a.keyframes[1].pose.bone_rotations[lower->id()] = -std::numbers::pi / 2;
    a.keyframes[2].pose.root_positions[rid] = {10, -10};
    a.keyframes[2].pose.bone_rotations[upper->id()] = 0;
    a.keyframes[2].pose.bone_rotations[lower->id()] = 0;

    auto world_position = [&](const sm::skeletal_pose& pose, sm::object_id id) {
        sm::topology copy;
        for (auto sid : f.rig)
            require(f.project.topology().skeleton(sid)->get().copy_to(copy).has_value(), "copy");
        sm::apply_skeletal_pose(pose, copy, f.rig);
        return copy.get<sm::node>(id)->get().world_pos();
    };

    auto first_mid = success(f.sample(a, 1.0));
    require(first_mid.pinned_nodes.contains(fid) && !first_mid.pinned_nodes.contains(rid),
        "first transition did not use its own pins");
    const auto planted_foot = world_position(first_mid.pose, fid);
    near(planted_foot.x, 20.0, 0.006);
    near(planted_foot.y, 0.0, 0.006);

    auto exact_second = success(f.sample(a, 2.0));
    same_pose(exact_second.pose, a.keyframes[1].pose);
    require(exact_second.pinned_nodes.contains(rid) && !exact_second.pinned_nodes.contains(fid),
        "exact non-terminal keyframe did not report outgoing transition pins");

    auto second_mid = success(f.sample(a, 3.5));
    require(second_mid.pinned_nodes.contains(rid) && !second_mid.pinned_nodes.contains(fid),
        "second transition did not use its own pins");
    const auto planted_root = world_position(second_mid.pose, rid);
    near(planted_root.x, 10.0);
    near(planted_root.y, -10.0);

    auto final = success(f.sample(a, 5.0));
    same_pose(final.pose, a.keyframes[2].pose);
    require(final.pinned_nodes.empty(), "final keyframe reported transition-local pins");

    auto invalid_membership = a;
    invalid_membership.transitions[0].pinned_nodes.insert(sm::object_id::generate());
    failed(f.sample(invalid_membership, 1.0), sm::result::invalid_membership);
}

void world_projection_and_failures() {
    fixture f;
    auto root = f.node({ 0, 0 }), tip = f.node({ 10, 0 });
    auto b = f.link(root, tip);
    auto a = f.sequence();
    // Both endpoints lie in [-135, 135] degrees; shortest arc crosses the
    // forbidden sector at 180 degrees. The nearest feasible boundary is 135.
    f.limit(b, sm::rotation_reference::world(), {-3*pi/4, 3*pi/2});
    a.keyframes[0].pose.bone_rotations[b->id()] = 2*pi/3;
    a.keyframes[1].pose.bone_rotations[b->id()] = -2*pi/3;
    const auto before = f.project.topology().to_json();
    const auto authored = a.keyframes[0].pose;
    f.feasible(success(f.sample(a, 0)).pose);
    f.feasible(success(f.sample(a, 2)).pose);
    auto result = f.sample(a, 0.8);
    near(success(result).pose.bone_rotations.at(b->id()), 3*pi/4);
    f.feasible(success(result).pose);
    auto feasible = a;
    feasible.keyframes[1].pose.bone_rotations[b->id()] = 0;
    same_pose(success(f.sample(feasible, 1)).pose, sm::sample_reference_pose(feasible, 1)->pose);
    auto invalid = a;
    invalid.keyframes[0].pose.bone_rotations[b->id()] = pi;
    failed(f.sample(invalid, 0), sm::result::unsatisfiable_constraints);
    require(invalid.keyframes[0].pose.bone_rotations.at(b->id()) == pi, "invalid endpoint repaired");
    auto incompatible = a;
    for (auto& k : incompatible.keyframes)
        k.pose.bone_rotations.clear();
    failed(f.sample(incompatible, 0), sm::result::invalid_membership);
    auto first = success(f.sample(a, 0.8));
    (void)f.sample(a, 1.6);
    same_pose(first.pose, success(f.sample(a, 0.8)).pose);
    f.limit(b, sm::rotation_reference::world(), {pi, 0});
    const auto contradictory = f.project.topology().to_json();
    failed(f.sample(a, 0.8), sm::result::unsatisfiable_constraints);
    require(f.project.topology().to_json() == contradictory, "failure mutation");
    same_pose(a.keyframes[0].pose, authored);
    require(before["skeletons"] == contradictory["skeletons"], "success mutation");
}
void parent_and_coupled_relations() {
    fixture f;
    auto root = f.node({0, 0}), middle = f.node({10, 0}), tip = f.node({20, 0});
    auto parent = f.link(root, middle), child = f.link(middle, tip);
    auto a = f.sequence();
    f.limit(parent, sm::rotation_reference::world(), {0.4, 0});
    f.limit(child, sm::rotation_reference::parent(), {-3*pi/4, 3*pi/2});
    for (auto& k : a.keyframes)
        k.pose.bone_rotations[parent->id()] = 0.4;
    a.keyframes[0].pose.bone_rotations[child->id()] = 2*pi/3;
    a.keyframes[1].pose.bone_rotations[child->id()] = -2*pi/3;
    auto result = f.sample(a, 0.8);
    near(success(result).pose.bone_rotations.at(parent->id()), 0.4);
    near(success(result).pose.bone_rotations.at(child->id()), 3*pi/4);
    f.feasible(success(result).pose);

    fixture g;
    auto r = g.node({0, 0}), p = g.node({10, 0}), q = g.node({0, 10});
    auto x = g.link(r, p), y = g.link(r, q);
    auto coupled = g.sequence();
    g.limit(y, sm::rotation_reference::bone(x->id()), {-3*pi/4, 3*pi/2});
    coupled.keyframes[0].pose.bone_rotations[x->id()] = 0;
    coupled.keyframes[1].pose.bone_rotations[x->id()] = 0;
    coupled.keyframes[0].pose.bone_rotations[y->id()] = 2*pi/3;
    coupled.keyframes[1].pose.bone_rotations[y->id()] = -2*pi/3;
    const auto reference = sm::sample_reference_pose(coupled, 0.8)->pose;
    auto solved = g.sample(coupled, 0.8);
    const auto& pose = success(solved).pose;
    const double correction = reference.bone_rotations.at(y->id()) - 3*pi/4;
    // Equal per-bone circular costs share the necessary correction equally;
    // treating x as a fixed reference and clamping only y is not the optimum.
    near(pose.bone_rotations.at(x->id()), correction/2, 1e-5);
    near(pose.bone_rotations.at(y->id()), reference.bone_rotations.at(y->id()) - correction/2, 1e-5);
    g.feasible(pose);
}
void coupled_equality_cycle() {
    fixture f;
    auto root = f.node({0, 0}), p = f.node({10, 0}), q = f.node({0, 10}), r = f.node({-10, 0});
    auto x = f.link(root, p), y = f.link(root, q), z = f.link(root, r);
    auto a = f.sequence();
    f.limit(y, sm::rotation_reference::bone(x->id()), {pi/2, 0});
    f.limit(z, sm::rotation_reference::bone(y->id()), {pi/2, 0});
    f.limit(z, sm::rotation_reference::bone(x->id()), {pi, 0});
    a.keyframes[1].pose.bone_rotations[x->id()] = pi;
    a.keyframes[1].pose.bone_rotations[y->id()] = -pi/2;
    a.keyframes[1].pose.bone_rotations[z->id()] = 0;
    f.feasible(success(f.sample(a, 0)).pose);
    f.feasible(success(f.sample(a, 2)).pose);
    auto result = f.sample(a, 0.8);
    f.feasible(success(result).pose);
    const auto& pose = success(result).pose;
    near(sm::angular_distance(pose.bone_rotations.at(y->id()) - pose.bone_rotations.at(x->id()), pi/2), 0);
    near(sm::angular_distance(pose.bone_rotations.at(z->id()) - pose.bone_rotations.at(x->id()), pi), 0);
}

void triangles() {
    for (double handedness : {1., -1.}) {
        fixture f;
        auto root = f.node({0, 0}), p = f.node({10, 0}), q = f.node({0, 7*handedness});
        auto x = f.link(root, p), y = f.link(root, q);
        require(f.project.add_rigid_triangle_constraint(x->id(), y->id()).has_value(), "triangle add");
        auto a = f.sequence();
        a.keyframes[0].pose.bone_rotations[x->id()] = 0;
        a.keyframes[0].pose.bone_rotations[y->id()] = handedness*pi/2;
        a.keyframes[1].pose.bone_rotations[x->id()] = handedness*pi;
        a.keyframes[1].pose.bone_rotations[y->id()] = -handedness*pi/2;
        // Opposite half-turn interpolation choices destroy the fan in the reference.
        auto reference = sm::sample_reference_pose(a, 1)->pose;
        require(std::abs(sm::angular_distance(reference.bone_rotations.at(y->id()) -
            reference.bone_rotations.at(x->id()), handedness*pi/2)) > 1, "triangle fixture must be invalid");
        const auto before = f.project.topology().to_json();
        auto result = f.sample(a, 1);
        const auto& pose = success(result).pose;
        near(sm::angular_distance(pose.bone_rotations.at(y->id()) - pose.bone_rotations.at(x->id()), handedness*pi/2), 0);
        f.feasible(pose);
        const double xangle = pose.bone_rotations.at(x->id()), yangle = pose.bone_rotations.at(y->id());
        const sm::point u{10*std::cos(xangle), 10*std::sin(xangle)}, v{7*std::cos(yangle), 7*std::sin(yangle)};
        near(sm::distance(u, v), std::sqrt(149.));
        require(handedness*(u.x*v.y-u.y*v.x) > 0, "triangle reflected");
        auto invalid = a;
        invalid.keyframes[0].pose = reference;
        failed(f.sample(invalid, 0), sm::result::unsatisfiable_constraints);
        require(f.project.topology().to_json() == before, "triangle mutation");
    }
}
void multiple_roots_scope_and_roundtrip() {
    fixture f;
    auto r = f.node({2, 3}), p = f.node({12, 3}), s = f.node({100, 50}), q = f.node({108, 50});
    auto x = f.link(r, p), y = f.link(s, q);
    auto a = f.sequence();
    f.limit(y, sm::rotation_reference::bone(x->id()), {-3*pi/4, 3*pi/2});
    a.keyframes[0].pose.bone_rotations[y->id()] = 2*pi/3;
    a.keyframes[1].pose.bone_rotations[y->id()] = -2*pi/3;
    a.keyframes[1].pose.root_positions[r->id()] = {12, 23};
    a.keyframes[1].pose.root_positions[s->id()] = {70, 10};
    const auto before = f.project.topology().to_json();
    const auto result = success(f.sample(a, 0.8));
    require(result.pose.root_positions == sm::sample_reference_pose(a, 0.8)->pose.root_positions, "multiple roots moved");
    f.feasible(result.pose);
    (void)f.sample(a, 1.7);
    same_pose(result.pose, success(f.sample(a, 0.8)).pose);

    sm::animation_assets assets;
    sm::initialize_animation_assets(assets, f.project.topology(), f.rig);
    assets.animations.push_back(a);
    const auto saved_assets = sm::animation_assets_to_json(assets);
    auto loaded = sm::animation_assets_from_json(saved_assets);
    sm::topology topology;
    auto reversed = before;
    std::reverse(reversed["skeletons"].begin(), reversed["skeletons"].end());
    for (auto& skeleton : reversed["skeletons"]) {
        std::reverse(skeleton["nodes"].begin(), skeleton["nodes"].end());
        std::reverse(skeleton["bones"].begin(), skeleton["bones"].end());
    }
    std::reverse(reversed["constraints"].begin(), reversed["constraints"].end());
    require(topology.from_json(reversed) == sm::result::success, "topology roundtrip");
    std::reverse(f.rig.begin(), f.rig.end());
    for (auto& k : loaded.animations.front().keyframes) {
        auto old = k.pose;
        k.pose.root_positions.clear();
        k.pose.bone_rotations.clear();
        for (auto id : { s->id(), r->id() })
            k.pose.root_positions.emplace(id, old.root_positions.at(id));
        for (auto id : { y->id(), x->id() })
            k.pose.bone_rotations.emplace(id, old.bone_rotations.at(id));
    }
    auto roundtrip = sm::sample_constrained_pose(loaded.animations.front(), 0.8, topology, f.rig);
    same_pose(result.pose, success(roundtrip).pose);
    require(sm::animation_assets_to_json(assets) == saved_assets, "assets mutation");
    require(f.project.topology().to_json() == before, "roundtrip source mutation");

    // Reject both outgoing and incoming cross-rig dependencies.
    for (auto skel : {x->owner().id(), y->owner().id()}) {
        f.rig = {skel};
        auto partial = a;
        for (auto& k : partial.keyframes)
            k.pose = sm::capture_skeletal_pose(f.project.topology(), f.rig);
        failed(f.sample(partial, 1), sm::result::invalid_membership);
        failed(f.sample(partial, 0), sm::result::invalid_membership);
    }
    require(f.project.topology().to_json() == before, "scope failure mutation");
}

void transition_rotation_constraints_are_local_and_roundtrip() {
    fixture f;
    auto root = f.node({0, 0}), tip = f.node({10, 0});
    auto bone = f.link(root, tip);
    auto a = f.sequence();
    a.keyframes[0].pose.bone_rotations[bone->id()] = -1.0;
    a.keyframes[1].pose.bone_rotations[bone->id()] = 1.0;
    a.keyframes[2].pose.bone_rotations[bone->id()] = 1.2;

    const auto cid = sm::object_id::generate();
    a.transitions[0].rotation_constraints.emplace(cid,
        sm::constraint{cid, "local", sm::rotation_constraint{
            bone->id(), sm::rotation_reference::world(), {0.4, 0.0}}});

    auto interior = success(f.sample(a, 1.0));
    near(interior.pose.bone_rotations.at(bone->id()), 0.4, 1e-6);
    require(interior.rotation_constraints.contains(cid), "active transition constraint not reported");

    auto exact0 = success(f.sample(a, 0.0));
    same_pose(exact0.pose, a.keyframes[0].pose);
    require(exact0.rotation_constraints.contains(cid), "outgoing constraint not reported at source key");
    auto exact1 = success(f.sample(a, 2.0));
    same_pose(exact1.pose, a.keyframes[1].pose);
    require(exact1.rotation_constraints.empty(), "unrelated outgoing transition constraint leaked");
    auto second = success(f.sample(a, 3.5));
    require(second.rotation_constraints.empty(), "inactive transition constraint applied");
    same_pose(second.pose, sm::sample_reference_pose(a, 3.5)->pose);

    sm::animation_assets assets;
    auto base = sm::capture_pose(f.project.topology(), f.rig, "Default");
    assets.default_pose = base.id;
    assets.poses.push_back(base);
    assets.animations.push_back(a);
    const auto json = sm::animation_assets_to_json(assets);
    auto loaded = sm::animation_assets_from_json(json);
    const auto& restored = loaded.animations.front().transitions.front().rotation_constraints.at(cid);
    const auto* rotation = restored.rotation();
    require(rotation && rotation->target_bone == bone->id(), "transition constraint target did not roundtrip");
    require(rotation->reference.kind == sm::rotation_reference_kind::world, "transition constraint reference did not roundtrip");
    near(rotation->allowed.start_angle, 0.4);
    near(rotation->allowed.span_angle, 0.0);
}

void transition_rotation_constraints_combine_with_persistent_and_pins() {
    fixture f;
    auto root = f.node({0, 0}), tip = f.node({10, 0});
    auto bone = f.link(root, tip);
    f.limit(bone, sm::rotation_reference::world(), {-0.5, 1.0});
    auto a = f.sequence();
    a.keyframes[0].pose.bone_rotations[bone->id()] = -0.25;
    a.keyframes[1].pose.bone_rotations[bone->id()] = 0.25;
    a.transitions[0].pinned_nodes.insert(root->id());
    const auto cid = sm::object_id::generate();
    a.transitions[0].rotation_constraints.emplace(cid,
        sm::constraint{cid, "local", sm::rotation_constraint{
            bone->id(), sm::rotation_reference::world(), {0.2, 0.0}}});
    auto sample = success(f.sample(a, 1.0));
    near(sample.pose.bone_rotations.at(bone->id()), 0.2, 1e-6);
    require(sample.pinned_nodes.contains(root->id()), "transition pin disappeared with local rotation constraint");

    auto bad = a;
    const auto bad_id = sm::object_id::generate();
    bad.transitions[0].rotation_constraints.clear();
    bad.transitions[0].rotation_constraints.emplace(bad_id,
        sm::constraint{bad_id, "bad", sm::rotation_constraint{
            sm::object_id::generate(), sm::rotation_reference::world(), {0, 1}}});
    failed(f.sample(bad, 1.0), sm::result::invalid_constraint);
}

void transition_parent_relative_rotation_constraint() {
    fixture f;
    auto root = f.node({0, 0}), joint = f.node({10, 0}), tip = f.node({20, 0});
    auto parent = f.link(root, joint);
    auto child = f.link(joint, tip);
    auto a = f.sequence();
    a.keyframes[0].pose.bone_rotations[parent->id()] = 0.2;
    a.keyframes[1].pose.bone_rotations[parent->id()] = 0.2;
    a.keyframes[0].pose.bone_rotations[child->id()] = -0.4;
    a.keyframes[1].pose.bone_rotations[child->id()] = 0.8;

    const auto cid = sm::object_id::generate();
    a.transitions[0].rotation_constraints.emplace(cid,
        sm::constraint{cid, "relative", sm::rotation_constraint{
            child->id(), sm::rotation_reference::parent(), {0.3, 0.0}}});

    auto sample = success(f.sample(a, 1.0));
    near(sample.pose.bone_rotations.at(child->id()), 0.3, 1e-6);
    require(sample.rotation_constraints.contains(cid),
        "parent-relative transition constraint not active");
}

void edge_cases_and_failure_isolation() {
    fixture roots;
    auto root1 = roots.node({1, 2}), root2 = roots.node({8, 9});
    auto roots_only = roots.sequence();
    roots_only.keyframes[1].pose.root_positions[root1->id()] = {3, 4};
    same_pose(success(roots.sample(roots_only, 1)).pose, sm::sample_reference_pose(roots_only, 1)->pose);
    roots.rig.push_back(roots.rig.front());
    failed(roots.sample(roots_only, 0), sm::result::invalid_membership);
    roots.rig = {sm::object_id::generate()};
    failed(roots.sample(roots_only, 0), sm::result::invalid_membership);

    fixture f;
    auto r = f.node({ 0, 0 }), p = f.node({ 1, 0 });
    auto x = f.link(r, p);
    auto a = f.sequence();
    const auto before = f.project.topology().to_json();
    // At this origin a one-unit horizontal bone collapses under double FK.
    // A coordinate-scaled roundoff tolerance must not approve the zero length.
    for (auto& key : a.keyframes)
        key.pose.root_positions[r->id()] = { 1e16, 0 };
    failed(f.sample(a, 0), sm::result::out_of_bounds);
    failed(f.sample(a, 1), sm::result::out_of_bounds);
    require(f.project.topology().to_json() == before, "unrepresentable FK changed topology");

    auto scaling = sm::scale_matrix(3);
    x->owner().apply(scaling);
    auto scaled = f.sequence();
    near(x->scale(), 3);
    f.limit(x, sm::rotation_reference::world(), {-3*pi/4, 3*pi/2});
    scaled.keyframes[0].pose.bone_rotations[x->id()] = 2*pi/3;
    scaled.keyframes[1].pose.bone_rotations[x->id()] = -2*pi/3;
    f.feasible(success(f.sample(scaled, 0.8)).pose);

    fixture translated;
    auto origin = translated.node({0, 0}), arm1 = translated.node({10, 0}), arm2 = translated.node({0, 10});
    auto t1 = translated.link(origin, arm1), t2 = translated.link(origin, arm2);
    require(translated.project.add_rigid_triangle_constraint(t1->id(), t2->id()).has_value(), "translated triangle");
    auto moved = translated.sequence();
    for (auto& key : moved.keyframes) {
        key.pose.root_positions[origin->id()] = {1e9, 0};
        key.pose.bone_rotations[t1->id()] = 0.2;
        key.pose.bone_rotations[t2->id()] = 0.2 + pi/2;
    }
    same_pose(success(translated.sample(moved, 0)).pose, moved.keyframes[0].pose);

    fixture g;
    auto base = g.node({0, 0}), u = g.node({10, 0}), v = g.node({0, 10}), w = g.node({-10, 0});
    auto b1 = g.link(base, u), b2 = g.link(base, v), b3 = g.link(base, w);
    auto impossible = g.sequence();
    g.limit(b1, sm::rotation_reference::bone(b2->id()), {0, 0});
    g.limit(b2, sm::rotation_reference::bone(b3->id()), {0, 0});
    g.limit(b3, sm::rotation_reference::bone(b1->id()), {0.1, 0});
    const auto constraints = sm::constraints_to_json(g.project.constraints());
    const auto geometry = g.project.topology().to_json();
    // Exhausted coupled charts are an honest no-solution outcome, with no pose.
    failed(g.sample(impossible, 1), sm::result::ik_no_solution_found);
    (void)g.sample(impossible, 1.7);
    failed(g.sample(impossible, 1), sm::result::ik_no_solution_found);
    require(sm::constraints_to_json(g.project.constraints()) == constraints &&
        g.project.topology().to_json() == geometry, "failed coupled solve mutation");
}
}
int main() {
    try {
        timing_and_exactness();
        transition_pins_govern_sampling_and_reporting();
        world_projection_and_failures();
        parent_and_coupled_relations();
        coupled_equality_cycle();
        triangles();
        multiple_roots_scope_and_roundtrip();
        transition_rotation_constraints_are_local_and_roundtrip();
        transition_rotation_constraints_combine_with_persistent_and_pins();
        transition_parent_relative_rotation_constraint();
        edge_cases_and_failure_isolation();
        std::cout << "PASS animation_v2_phase3b\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
