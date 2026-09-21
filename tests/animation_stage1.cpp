#include "core/sm_animation.hpp"
#include "core/sm_skeleton.hpp"
#include "core/sm_bone.hpp"
#include "json.hpp"

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

bool near(double a, double b, double eps = 1e-6) {
    return std::abs(a - b) < eps;
}

void motion_paths_are_persistent_displacement_paths() {
    sm::motion_path line(sm::line_path{{0.0, 0.0}, {10.0, 0.0}});
    require(line.kind() == sm::motion_path_kind::straight, "line path kind is wrong");
    const auto halfway = line.evaluate_by_arc_length(0.5);
    require(near(halfway.x, 5.0) && near(halfway.y, 0.0), "line path midpoint is wrong");

    sm::spline_path spline;
    spline.segments.push_back({{0.0, 0.0}, {3.0, 0.0}, {7.0, 10.0}, {10.0, 10.0}});
    spline.segments.push_back({{10.0, 10.0}, {13.0, 10.0}, {17.0, 0.0}, {20.0, 0.0}});
    sm::motion_path path(std::move(spline));
    require(path.kind() == sm::motion_path_kind::spline, "spline path kind is wrong");
    const auto end = path.final_displacement();
    require(near(end.x, 20.0) && near(end.y, 0.0), "spline final displacement is wrong");
}

void translation_actions_round_trip_with_bone_reference() {
    sm::animation_assets assets;

    sm::pose base;
    base.name = "Default";
    const auto pose_node = sm::object_id::generate();
    base.node_positions.emplace(pose_node, sm::point{1.0, 2.0});
    assets.default_pose = base.id;
    assets.poses.push_back(base);

    const auto skeleton_id = sm::object_id::generate();
    const auto reference_bone = sm::object_id::generate();
    const auto effector = sm::object_id::generate();
    const auto pin = sm::object_id::generate();

    sm::animation animation;
    animation.name = "translations";
    animation.base_pose = base.id;

    sm::animation_action rigid_action;
    rigid_action.start = 0;
    rigid_action.duration = 500;
    sm::rigid_translation rigid;
    rigid.skeletons = {skeleton_id};
    rigid.path = sm::motion_path(sm::line_path{{0.0, 0.0}, {12.0, 4.0}});
    rigid.reference = sm::translation_reference::bone;
    rigid.reference_bone = reference_bone;
    rigid_action.data = rigid;

    sm::animation_action ik_action;
    ik_action.start = 500;
    ik_action.duration = 500;
    sm::ik_translation ik;
    ik.effector = effector;
    ik.pins = {pin};
    sm::spline_path spline;
    spline.segments.push_back({{0.0, 0.0}, {2.0, 0.0}, {4.0, 3.0}, {6.0, 3.0}});
    ik.path = sm::motion_path(std::move(spline));
    ik.reference = sm::translation_reference::bone;
    ik.reference_bone = reference_bone;
    ik.effector_start = {5.0, -2.0};
    ik_action.data = ik;

    animation.layers.push_back({{rigid_action, ik_action}});
    assets.animations.push_back(animation);

    const auto json = sm::animation_assets_to_json(assets);
    const auto restored = sm::animation_assets_from_json(json);
    require(restored.animations.size() == 1, "animation round trip lost animation");
    require(restored.animations.front().layers.size() == 1, "animation round trip lost layer");
    require(restored.animations.front().layers.front().actions.size() == 2, "animation round trip lost actions");

    const auto& restored_rigid = std::get<sm::rigid_translation>(
        restored.animations.front().layers.front().actions[0].data);
    require(restored_rigid.reference == sm::translation_reference::bone, "rigid translation reference changed");
    require(restored_rigid.reference_bone == reference_bone, "rigid translation reference bone changed");
    require(restored_rigid.skeletons == std::vector<sm::object_id>{skeleton_id}, "rigid translation targets changed");

    const auto& restored_ik = std::get<sm::ik_translation>(
        restored.animations.front().layers.front().actions[1].data);
    require(restored_ik.reference == sm::translation_reference::bone, "IK translation reference changed");
    require(restored_ik.reference_bone == reference_bone, "IK translation reference bone changed");
    require(restored_ik.effector == effector, "IK translation effector changed");
    require(restored_ik.pins == std::vector<sm::object_id>{pin}, "IK translation pins changed");
    require(near(restored_ik.effector_start.x, 5.0) && near(restored_ik.effector_start.y, -2.0),
        "IK translation start changed");
    require(std::holds_alternative<sm::spline_path>(restored_ik.path.geometry()),
        "IK translation spline was not preserved");
}

void root_reference_frames_use_the_character_root_bone() {
    sm::topology topology;
    auto& root = topology.create_skeleton(sm::point{0.0, 0.0});
    auto& tip = topology.create_skeleton(sm::point{0.0, 10.0});
    auto created = topology.create_bone("character-root", root.root_node(), tip.root_node());
    require(created.has_value(), "failed to create root-frame fixture");

    const auto root_bone = created->get().id();
    const auto skeleton = created->get().owner().id();
    const auto base = sm::capture_pose(topology, {skeleton}, "base");

    sm::animation animation;
    animation.base_pose = base.id;
    sm::animation_action action;
    action.start = 0;
    action.duration = 1000;
    sm::rigid_translation translation;
    translation.skeletons = {skeleton};
    translation.reference = sm::translation_reference::animation_root;
    translation.path = sm::motion_path(sm::line_path{{0.0, 0.0}, {10.0, 0.0}});
    action.data = translation;
    animation.layers.push_back({{action}});

    const auto report = sm::evaluate_animation(animation, base, root_bone, topology, 1000);
    require(report.invalid_actions.empty(), "root-frame translation evaluated as invalid");
    const auto bone = topology.get<sm::bone>(root_bone);
    require(bone.has_value(), "root bone disappeared during evaluation");
    const auto position = bone->get().parent_node().world_pos();
    require(near(position.x, 0.0) && near(position.y, 10.0),
        "Animation Root did not use the character root bone's starting orientation");
}

struct frame_fixture {
    sm::topology topology;
    sm::object_id skeleton_id;
    sm::object_id root_bone_id;
    sm::object_id child_bone_id;
    sm::pose base;

    frame_fixture() {
        auto& root = topology.create_skeleton(sm::point{0.0, 0.0});
        auto& middle = topology.create_skeleton(sm::point{10.0, 0.0});
        auto& tip = topology.create_skeleton(sm::point{20.0, 0.0});
        auto& root_node = root.root_node();
        auto& middle_node = middle.root_node();
        auto& tip_node = tip.root_node();
        auto root_bone = topology.create_bone("root", root_node, middle_node);
        require(root_bone.has_value(), "failed to create frame fixture root bone");
        auto child_bone = topology.create_bone("child", middle_node, tip_node);
        require(child_bone.has_value(), "failed to create frame fixture child bone");
        skeleton_id = root_bone->get().owner().id();
        root_bone_id = root_bone->get().id();
        child_bone_id = child_bone->get().id();
        base = sm::capture_pose(topology, {skeleton_id}, "base");
    }
};

sm::animation_action translate_action(sm::object_id skeleton, sm::point displacement) {
    sm::animation_action action;
    action.start = 0;
    action.duration = 1000;
    action.data = sm::rigid_translation{{skeleton},
        sm::motion_path(sm::line_path{{0.0, 0.0}, displacement}),
        sm::translation_reference::animation_root,{}};
    return action;
}

sm::animation_action rotate_action(sm::object_id bone, sm::animation_time start = 0) {
    sm::animation_action action;
    action.start = start;
    action.duration = 1000;
    action.data = sm::rigid_rotation{bone,sm::rotation_pivot::root,std::acos(-1.0)/2.0,
        sm::rotation_propagation::hierarchy};
    return action;
}

void character_root_frame_follows_earlier_translation() {
    frame_fixture f;
    sm::animation animation; animation.base_pose = f.base.id;
    animation.layers.push_back({{translate_action(f.skeleton_id,{5.0,3.0})}});
    sm::evaluate_animation(animation,f.base,f.root_bone_id,f.topology,1000);

    const auto frame=sm::translation_reference_frame(sm::translation_reference::character_root,{},
        f.root_bone_id,f.base,f.topology);
    require(frame.has_value(), "character-root frame is missing after translation");
    require(near(frame->origin.x,5.0) && near(frame->origin.y,3.0),
        "Character Root frame did not follow an earlier translation");
    require(near(sm::angular_distance(frame->angle,0.0),0.0),
        "Character Root frame orientation changed during pure translation");
}

void character_root_frame_follows_earlier_rotation() {
    frame_fixture f;
    sm::animation animation; animation.base_pose = f.base.id;
    animation.layers.push_back({{rotate_action(f.root_bone_id)}});
    sm::evaluate_animation(animation,f.base,f.root_bone_id,f.topology,1000);

    const auto frame=sm::translation_reference_frame(sm::translation_reference::character_root,{},
        f.root_bone_id,f.base,f.topology);
    require(frame.has_value(), "character-root frame is missing after rotation");
    require(near(frame->origin.x,0.0) && near(frame->origin.y,0.0),
        "Character Root frame origin moved during root-pivot rotation");
    require(near(sm::angular_distance(frame->angle,std::acos(-1.0)/2.0),0.0),
        "Character Root frame did not follow an earlier rotation");
}

void bone_frame_follows_earlier_translation_and_rotation() {
    frame_fixture f;
    sm::animation animation; animation.base_pose = f.base.id;
    animation.layers.push_back({{translate_action(f.skeleton_id,{4.0,5.0}),rotate_action(f.root_bone_id,1000)}});
    sm::evaluate_animation(animation,f.base,f.root_bone_id,f.topology,2000);

    const auto frame=sm::translation_reference_frame(sm::translation_reference::bone,f.child_bone_id,
        f.root_bone_id,f.base,f.topology);
    require(frame.has_value(), "bone-relative frame is missing");
    require(near(frame->origin.x,4.0) && near(frame->origin.y,15.0),
        "Bone reference origin did not follow earlier translation/rotation");
    require(near(sm::angular_distance(frame->angle,std::acos(-1.0)/2.0),0.0),
        "Bone reference orientation did not follow earlier translation/rotation");
}

void animation_root_frame_stays_in_base_pose() {
    frame_fixture f;
    sm::animation animation; animation.base_pose = f.base.id;
    animation.layers.push_back({{translate_action(f.skeleton_id,{4.0,5.0}),rotate_action(f.root_bone_id,1000)}});
    sm::evaluate_animation(animation,f.base,f.root_bone_id,f.topology,2000);

    const auto frame=sm::translation_reference_frame(sm::translation_reference::animation_root,{},
        f.root_bone_id,f.base,f.topology);
    require(frame.has_value(), "animation-root frame is missing");
    require(near(frame->origin.x,0.0) && near(frame->origin.y,0.0),
        "Animation Root origin followed the evaluated topology instead of the base pose");
    require(near(sm::angular_distance(frame->angle,0.0),0.0),
        "Animation Root orientation followed the evaluated topology instead of the base pose");
}

void reference_frame_round_trips_points_and_vectors() {
    const sm::reference_frame frame{{17.0,-9.0},0.73};
    const sm::point local{3.5,-8.25};
    const auto world=frame.local_to_world(local);
    const auto restored=frame.world_to_local(world);
    require(near(restored.x,local.x) && near(restored.y,local.y),
        "reference-frame point local/world round trip failed");

    const sm::point vector{-2.0,6.0};
    const auto world_vector=frame.vector_to_world(vector);
    const auto restored_vector=frame.vector_to_local(world_vector);
    require(near(restored_vector.x,vector.x) && near(restored_vector.y,vector.y),
        "reference-frame vector local/world round trip failed");
}

struct composition_fixture {
    sm::topology topology;
    sm::object_id reference_skeleton;
    sm::object_id payload_skeleton;
    sm::object_id root_bone;
    sm::object_id child_bone;
    sm::object_id payload_node;
    sm::pose base;

    composition_fixture() {
        auto& root = topology.create_skeleton(sm::point{0.0,0.0});
        auto& middle = topology.create_skeleton(sm::point{10.0,0.0});
        auto& tip = topology.create_skeleton(sm::point{20.0,0.0});
        // Joining skeletons destroys the absorbed skeleton wrapper; node refs survive.
        auto& root_node = root.root_node();
        auto& middle_node = middle.root_node();
        auto& tip_node = tip.root_node();
        auto first = topology.create_bone("root",root_node,middle_node);
        require(first.has_value(),"failed to create composition root bone");
        auto second = topology.create_bone("child",middle_node,tip_node);
        require(second.has_value(),"failed to create composition child bone");
        reference_skeleton=first->get().owner().id();
        root_bone=first->get().id();
        child_bone=second->get().id();
        auto& payload=topology.create_skeleton(sm::point{100.0,100.0});
        payload_skeleton=payload.id();
        payload_node=payload.root_node().id();
        base=sm::capture_pose(topology,{reference_skeleton,payload_skeleton},"base");
    }
};

sm::animation_action relative_translation(sm::object_id target, sm::translation_reference reference,
        sm::object_id reference_bone = {}, sm::point displacement = {10.0,0.0}) {
    sm::animation_action action;
    action.start=0;action.duration=1000;
    action.data=sm::rigid_translation{{target},sm::motion_path(sm::line_path{{0.0,0.0},displacement}),reference,reference_bone};
    return action;
}

void character_root_placement_follows_later_layer_translation() {
    composition_fixture f;
    auto follow=relative_translation(f.payload_skeleton,sm::translation_reference::character_root);
    auto move_root=translate_action(f.reference_skeleton,{20.0,10.0});
    sm::animation animation;animation.base_pose=f.base.id;
    animation.layers.push_back({{follow}});     // ordinary order: follow first
    animation.layers.push_back({{move_root}}); // placement must put follow above this

    animation=sm::place_animation_action(animation,follow.id,f.root_bone,f.topology);
    const auto report=sm::evaluate_animation(animation,f.base,f.root_bone,f.topology,500);
    require(report.evaluation_order.size()==2 && report.evaluation_order[0]==move_root.id && report.evaluation_order[1]==follow.id,
        "Character Root placement did not put the consumer above the producer");
    const auto context=report.contexts.at(follow.id).translation_reference_frame;
    require(context.has_value(),"Character Root translation did not record its evaluation frame");
    require(near(context->origin.x,10.0)&&near(context->origin.y,5.0),
        "Character Root translation saw the base frame instead of the animated frame");
    const auto payload=f.topology.get<sm::node>(f.payload_node)->get().world_pos();
    require(near(payload.x,105.0)&&near(payload.y,100.0),
        "Character Root-relative translation did not use the placed frame");
}

void character_root_placement_follows_rotation() {
    composition_fixture f;
    auto follow=relative_translation(f.payload_skeleton,sm::translation_reference::character_root);
    auto rotate=rotate_action(f.root_bone);
    sm::animation animation;animation.base_pose=f.base.id;
    animation.layers.push_back({{follow}});
    animation.layers.push_back({{rotate}});

    animation=sm::place_animation_action(animation,follow.id,f.root_bone,f.topology);
    const auto report=sm::evaluate_animation(animation,f.base,f.root_bone,f.topology,1000);
    const auto context=report.contexts.at(follow.id).translation_reference_frame;
    require(context.has_value(),"rotated Character Root frame was not recorded");
    require(near(sm::angular_distance(context->angle,std::acos(-1.0)/2.0),0.0),
        "Character Root-relative translation did not see root rotation");
    const auto payload=f.topology.get<sm::node>(f.payload_node)->get().world_pos();
    require(near(payload.x,100.0)&&near(payload.y,110.0),
        "Character Root-relative local X did not rotate into world Y");
}

void bone_placement_follows_later_translation_and_rotation() {
    composition_fixture f;
    auto follow=relative_translation(f.payload_skeleton,sm::translation_reference::bone,f.child_bone);
    auto move=translate_action(f.reference_skeleton,{4.0,5.0});
    auto rotate=rotate_action(f.root_bone);
    sm::animation animation;animation.base_pose=f.base.id;
    animation.layers.push_back({{follow}});
    animation.layers.push_back({{move}});
    animation.layers.push_back({{rotate}});

    animation=sm::place_animation_action(animation,follow.id,f.root_bone,f.topology);
    const auto report=sm::evaluate_animation(animation,f.base,f.root_bone,f.topology,1000);
    require(report.evaluation_order.size()==3 && report.evaluation_order.back()==follow.id,
        "Bone reference placement did not move the consumer after its producers");
    const auto context=report.contexts.at(follow.id).translation_reference_frame;
    require(context.has_value(),"Bone-relative translation did not record its frame");
    require(near(context->origin.x,4.0)&&near(context->origin.y,15.0),
        "Bone-relative frame origin did not follow preceding translation/rotation");
    require(near(sm::angular_distance(context->angle,std::acos(-1.0)/2.0),0.0),
        "Bone-relative frame orientation did not follow preceding rotation");
}

void animation_root_has_no_animation_dependencies() {
    composition_fixture f;
    auto fixed=relative_translation(f.payload_skeleton,sm::translation_reference::animation_root);
    auto move=translate_action(f.reference_skeleton,{20.0,10.0});
    auto rotate=rotate_action(f.root_bone);
    sm::animation animation;animation.base_pose=f.base.id;
    animation.layers.push_back({{fixed}});
    animation.layers.push_back({{move}});
    animation.layers.push_back({{rotate}});

    const auto report=sm::evaluate_animation(animation,f.base,f.root_bone,f.topology,1000);
    require(report.evaluation_order.front()==fixed.id,"Animation Root incorrectly acquired animation dependencies");
    const auto context=report.contexts.at(fixed.id).translation_reference_frame;
    require(context.has_value(),"Animation Root frame was not recorded");
    require(near(context->origin.x,0.0)&&near(context->origin.y,0.0)&&near(sm::angular_distance(context->angle,0.0),0.0),
        "Animation Root did not remain fixed to the base pose");
}

void direct_time_evaluation_is_history_independent() {
    composition_fixture f;
    auto follow=relative_translation(f.payload_skeleton,sm::translation_reference::character_root,{}, {18.0,6.0});
    auto move=translate_action(f.reference_skeleton,{20.0,10.0});
    auto rotate=rotate_action(f.root_bone);
    sm::animation animation;animation.base_pose=f.base.id;
    animation.layers.push_back({{follow}});animation.layers.push_back({{move}});animation.layers.push_back({{rotate}});

    sm::evaluate_animation(animation,f.base,f.root_bone,f.topology,100);
    sm::evaluate_animation(animation,f.base,f.root_bone,f.topology,500);
    sm::evaluate_animation(animation,f.base,f.root_bone,f.topology,800);
    const auto after_history=f.topology.get<sm::node>(f.payload_node)->get().world_pos();
    sm::evaluate_animation(animation,f.base,f.root_bone,f.topology,800);
    const auto direct=f.topology.get<sm::node>(f.payload_node)->get().world_pos();
    require(near(after_history.x,direct.x)&&near(after_history.y,direct.y),
        "absolute-time evaluation depends on previously evaluated frames");
}

void unrelated_actions_keep_ordinary_order() {
    composition_fixture f;
    auto first=relative_translation(f.payload_skeleton,sm::translation_reference::animation_root,{}, {1.0,0.0});
    auto second=relative_translation(f.payload_skeleton,sm::translation_reference::animation_root,{}, {0.0,2.0});
    auto third=relative_translation(f.payload_skeleton,sm::translation_reference::animation_root,{}, {3.0,0.0});
    sm::animation animation;animation.base_pose=f.base.id;
    animation.layers.push_back({{first}});animation.layers.push_back({{second}});animation.layers.push_back({{third}});
    const auto order=sm::animation_evaluation_order(animation);
    require(order==std::vector<sm::object_id>{first.id,second.id,third.id},
        "unrelated actions did not retain ordinary layer order");
}

void circular_reference_dependency_is_rejected() {
    sm::topology topology;
    auto& a0=topology.create_skeleton(sm::point{0.0,0.0});auto& a1=topology.create_skeleton(sm::point{10.0,0.0});
    auto a_bone=topology.create_bone("a",a0.root_node(),a1.root_node());require(a_bone.has_value(),"failed cycle A bone");
    auto& b0=topology.create_skeleton(sm::point{0.0,20.0});auto& b1=topology.create_skeleton(sm::point{10.0,20.0});
    auto b_bone=topology.create_bone("b",b0.root_node(),b1.root_node());require(b_bone.has_value(),"failed cycle B bone");
    const auto a_skel=a_bone->get().owner().id(),b_skel=b_bone->get().owner().id();
    auto a=relative_translation(a_skel,sm::translation_reference::bone,b_bone->get().id());
    auto b=relative_translation(b_skel,sm::translation_reference::bone,a_bone->get().id());
    const auto base=sm::capture_pose(topology,{a_skel,b_skel},"base");
    sm::animation animation;animation.base_pose=base.id;animation.layers.push_back({{a}});animation.layers.push_back({{b}});
    bool rejected=false;
    try{sm::validate_animation_order(animation,a_bone->get().id(),topology);}
    catch(const std::invalid_argument& error){rejected=true;}
    require(rejected,"circular reference dependency was not rejected");

    sm::animation_assets assets;assets.default_pose=base.id;assets.poses.push_back(base);assets.animations.push_back(animation);
    rejected=false;
    try{assets.validate(topology,a_bone->get().id());}
    catch(const std::invalid_argument&){rejected=true;}
    require(rejected,"animation asset validation did not reject a circular dependency");
}

void self_reference_does_not_create_dependency() {
    frame_fixture f;
    auto self=relative_translation(f.skeleton_id,sm::translation_reference::character_root);
    sm::animation animation;animation.base_pose=f.base.id;animation.layers.push_back({{self}});
    const auto order=sm::animation_evaluation_order(animation);
    require(order==std::vector<sm::object_id>{self.id},"an action was made dependent on itself");
    const auto report=sm::evaluate_animation(animation,f.base,f.root_bone_id,f.topology,1000);
    require(report.invalid_actions.empty(),"self-referential frame action was rejected despite pre-action semantics");
}

void translation_context_is_the_frame_used_for_application() {
    composition_fixture f;
    auto follow=relative_translation(f.payload_skeleton,sm::translation_reference::character_root,{}, {7.0,3.0});
    auto rotate=rotate_action(f.root_bone);
    sm::animation animation;animation.base_pose=f.base.id;animation.layers.push_back({{follow}});animation.layers.push_back({{rotate}});
    const auto report=sm::evaluate_animation(animation,f.base,f.root_bone,f.topology,1000);
    const auto frame=report.contexts.at(follow.id).translation_reference_frame;
    require(frame.has_value(),"translation context is missing its reference frame");
    const auto expected=sm::point{100.0,100.0}+frame->vector_to_world({7.0,3.0});
    const auto actual=f.topology.get<sm::node>(f.payload_node)->get().world_pos();
    require(near(expected.x,actual.x)&&near(expected.y,actual.y),
        "recorded translation context differs from the frame used to apply the action");
}

} // namespace

int main() {
    try {
        motion_paths_are_persistent_displacement_paths();
        translation_actions_round_trip_with_bone_reference();
        root_reference_frames_use_the_character_root_bone();
        character_root_frame_follows_earlier_translation();
        character_root_frame_follows_earlier_rotation();
        bone_frame_follows_earlier_translation_and_rotation();
        animation_root_frame_stays_in_base_pose();
        reference_frame_round_trips_points_and_vectors();
        character_root_placement_follows_later_layer_translation();
        character_root_placement_follows_rotation();
        bone_placement_follows_later_translation_and_rotation();
        animation_root_has_no_animation_dependencies();
        direct_time_evaluation_is_history_independent();
        unrelated_actions_keep_ordinary_order();
        circular_reference_dependency_is_rejected();
        self_reference_does_not_create_dependency();
        translation_context_is_the_frame_used_for_application();
        std::cout << "PASS animation_stage1\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
