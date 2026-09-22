#include "model/project.hpp"
#include "core/sm_animation.hpp"
#include "core/sm_skeleton.hpp"
#include "core/sm_bone.hpp"
#include "json.hpp"

#include <algorithm>
#include <iostream>
#include <ranges>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

struct chain_fixture {
    mdl::project model;
    sm::object_id character;
    sm::object_id skeleton;
    sm::object_id root;
    sm::object_id middle;
    sm::object_id tip;
    sm::object_id root_bone;
    sm::object_id child_bone;

    chain_fixture() {
        auto& a = model.core().create_skeleton({0.0, 0.0});
        auto& b = model.core().create_skeleton({10.0, 0.0});
        auto& c = model.core().create_skeleton({20.0, 0.0});
        sm::node_ref root_node(a.root_node());
        sm::node_ref middle_node(b.root_node());
        sm::node_ref tip_node(c.root_node());
        root = root_node->id();
        middle = middle_node->id();
        tip = tip_node->id();
        auto first = model.core().create_bone("root", *root_node, *middle_node);
        require(first.has_value(), "failed to create root bone");
        root_bone = first->get().id();
        auto second = model.core().create_bone("child", *middle_node, *tip_node);
        require(second.has_value(), "failed to create child bone");
        child_bone = second->get().id();
        skeleton = second->get().owner().id();
        std::vector<sm::const_skel_ref> rig{*model.core().topology().skeleton(skeleton)};
        auto created = model.core().create_character(rig);
        require(created.has_value(), "failed to create character");
        character = created->get().id();
    }

    sm::animation& animation() {
        auto& assets = model.core().animation_data(character);
        if (assets.animations.empty()) {
            sm::animation animation;
            animation.name = "test";
            animation.base_pose = assets.default_pose;
            assets.animations.push_back(std::move(animation));
        }
        return assets.animations.front();
    }

    sm::animation_action add_rotation(sm::object_id bone, sm::animation_time start = 0, std::size_t layer = 0) {
        auto& animation = this->animation();
        while (animation.layers.size() <= layer) animation.layers.push_back({});
        sm::animation_action action;
        action.start = start;
        action.duration = 50;
        action.data = sm::rigid_rotation{bone, sm::rotation_pivot::root, 0.75,
            sm::rotation_propagation::hierarchy};
        animation.layers[layer].actions.push_back(action);
        return action;
    }

    sm::animation_action add_ik(sm::object_id effector, std::vector<sm::object_id> pins,
            sm::object_id reference_bone = {}, sm::animation_time start = 0, std::size_t layer = 0) {
        auto& animation = this->animation();
        while (animation.layers.size() <= layer) animation.layers.push_back({});
        sm::animation_action action;
        action.start = start;
        action.duration = 50;
        sm::ik_translation data;
        data.effector = effector;
        data.pins = std::move(pins);
        data.path = sm::motion_path(sm::line_path{{0.0, 0.0}, {3.0, 2.0}});
        if (!reference_bone.is_nil()) {
            data.reference = sm::translation_reference::bone;
            data.reference_bone = reference_bone;
        }
        action.data = std::move(data);
        animation.layers[layer].actions.push_back(action);
        return action;
    }

    sm::animation_action add_rigid_translation(sm::object_id target, sm::animation_time start = 0, std::size_t layer = 0) {
        auto& animation = this->animation();
        while (animation.layers.size() <= layer) animation.layers.push_back({});
        sm::animation_action action;
        action.start = start;
        action.duration = 50;
        sm::rigid_translation data;
        data.skeletons = {target};
        data.path = sm::motion_path(sm::line_path{{0.0, 0.0}, {4.0, 0.0}});
        action.data = std::move(data);
        animation.layers[layer].actions.push_back(action);
        return action;
    }
};

sm::skel_ref component(sm::topology& out, const chain_fixture& f,
        std::vector<sm::object_id> nodes, std::vector<sm::object_id> bones) {
    auto created = out.create_skeleton("component");
    require(created.has_value(), "failed to create scratch component");
    auto skel = *created;
    for (const auto id : nodes) {
        auto node = f.model.core().topology().get<sm::node>(id);
        require(node.has_value(), "source node missing");
        require(node->get().copy_to(out, skel->id()).has_value(), "failed to copy node");
    }
    for (const auto id : bones) {
        auto bone = f.model.core().topology().get<sm::bone>(id);
        require(bone.has_value(), "source bone missing");
        require(bone->get().copy_to(out, skel->id()).has_value(), "failed to copy bone");
    }
    return skel;
}

sm::topology delete_child_bone_replacements(const chain_fixture& f) {
    sm::topology replacements;
    component(replacements, f, {f.root, f.middle}, {f.root_bone});
    component(replacements, f, {f.tip}, {});
    return replacements;
}

sm::topology delete_root_bone_replacements(const chain_fixture& f) {
    sm::topology replacements;
    component(replacements, f, {f.root}, {});
    component(replacements, f, {f.middle, f.tip}, {f.child_bone});
    return replacements;
}

sm::topology delete_tip_replacements(const chain_fixture& f) {
    sm::topology replacements;
    component(replacements, f, {f.root, f.middle}, {f.root_bone});
    return replacements;
}

sm::topology delete_root_replacements(const chain_fixture& f) {
    sm::topology replacements;
    component(replacements, f, {f.middle, f.tip}, {f.child_bone});
    return replacements;
}

std::vector<sm::skel_ref> refs(sm::topology& topology) {
    return topology.skeletons() | std::ranges::to<std::vector<sm::skel_ref>>();
}

bool action_exists(const sm::animation_assets& assets, sm::object_id id) {
    for (const auto& animation : assets.animations)
        for (const auto& layer : animation.layers)
            if (std::ranges::any_of(layer.actions, [&](const auto& action) { return action.id == id; })) return true;
    return false;
}

void dependency_enumerator_is_complete() {
    const auto a = sm::object_id::generate();
    const auto b = sm::object_id::generate();
    const auto c = sm::object_id::generate();
    const auto d = sm::object_id::generate();

    sm::animation_action rotation;
    rotation.data = sm::rigid_rotation{a};
    require(sm::animation_action_dependencies(rotation) ==
        std::vector<sm::animation_dependency>{{sm::animation_dependency_kind::bone, a}},
        "rigid rotation dependency is wrong");

    sm::animation_action ik_rotation;
    ik_rotation.data = sm::ik_rotation{a, b, 0.5};
    require(sm::animation_action_dependencies(ik_rotation) == std::vector<sm::animation_dependency>{
        {sm::animation_dependency_kind::node, a}, {sm::animation_dependency_kind::node, b}},
        "IK rotation dependencies are wrong");

    sm::animation_action rigid_translation;
    sm::rigid_translation rigid;
    rigid.skeletons = {a, b};
    rigid.reference = sm::translation_reference::bone;
    rigid.reference_bone = c;
    rigid_translation.data = rigid;
    require(sm::animation_action_dependencies(rigid_translation) == std::vector<sm::animation_dependency>{
        {sm::animation_dependency_kind::skeleton, a}, {sm::animation_dependency_kind::skeleton, b},
        {sm::animation_dependency_kind::bone, c}}, "rigid translation dependencies are wrong");

    sm::animation_action ik_translation;
    sm::ik_translation ik;
    ik.effector = a;
    ik.pins = {b, c};
    ik.reference = sm::translation_reference::bone;
    ik.reference_bone = d;
    ik_translation.data = ik;
    require(sm::animation_action_dependencies(ik_translation) == std::vector<sm::animation_dependency>{
        {sm::animation_dependency_kind::node, a}, {sm::animation_dependency_kind::node, b},
        {sm::animation_dependency_kind::node, c}, {sm::animation_dependency_kind::bone, d}},
        "IK translation dependencies are wrong");
}

void validation_rejects_unresolved_action_reference() {
    chain_fixture f;
    f.add_rotation(sm::object_id::generate());
    bool rejected = false;
    try {
        f.model.core().animation_data(f.character).validate(
            f.model.core().topology(),
            f.model.core().character(f.character)->get().character_root_bone());
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    require(rejected, "animation validation accepted an unresolved action reference");
}

void bone_target_delete_undo_redo_exact() {
    chain_fixture f;
    const auto action = f.add_rotation(f.child_bone);
    auto& assets = f.model.core().animation_data(f.character);
    const auto before = sm::animation_assets_to_json(assets);
    auto replacements = delete_child_bone_replacements(f);
    require(f.model.replace_skeletons({f.skeleton}, refs(replacements)) == sm::result::success,
        "bone deletion failed");
    require(!action_exists(assets, action.id), "bone-target rotation action survived deletion");
    require(f.model.core().has_valid_animation_references(), "deletion left dangling action references");

    f.model.undo();
    require(sm::animation_assets_to_json(f.model.core().animation_data(f.character)) == before,
        "undo did not restore the complete animation payload");
    require(f.model.core().topology().get<sm::bone>(f.child_bone).has_value(), "undo did not restore bone");
    require(f.model.core().has_valid_animation_references(), "undo restored dangling references");

    require(f.model.redo() == sm::result::success, "redo failed");
    require(!action_exists(f.model.core().animation_data(f.character), action.id), "redo did not remove action");
    require(!f.model.core().topology().get<sm::bone>(f.child_bone), "redo did not remove bone");
}

void ik_effector_and_pin_cascade() {
    {
        chain_fixture f;
        const auto action = f.add_ik(f.tip, {f.root});
        auto replacements = delete_tip_replacements(f);
        require(f.model.replace_skeletons({f.skeleton}, refs(replacements)) == sm::result::success,
            "effector deletion failed");
        require(!action_exists(f.model.core().animation_data(f.character), action.id),
            "IK action survived effector deletion");
    }
    {
        chain_fixture f;
        const auto action = f.add_ik(f.tip, {f.root});
        auto replacements = delete_root_replacements(f);
        require(f.model.replace_skeletons({f.skeleton}, refs(replacements)) == sm::result::success,
            "pin deletion failed");
        require(!action_exists(f.model.core().animation_data(f.character), action.id),
            "IK action survived persistent-pin deletion");
    }
}

void bone_reference_only_cascades() {
    chain_fixture f;
    auto action = f.add_ik(f.tip, {f.middle}, f.root_bone);
    auto& stored = f.animation().layers.front().actions.back();
    stored.easing = sm::easing::ease_in_out;
    sm::spline_path spline;
    spline.segments.push_back({{0.0, 0.0}, {1.0, 0.0}, {2.0, 2.0}, {3.0, 2.0}});
    spline.segments.push_back({{3.0, 2.0}, {4.0, 2.0}, {5.0, 0.0}, {6.0, 0.0}});
    std::get<sm::ik_translation>(stored.data).path = sm::motion_path(std::move(spline));
    action = stored;
    const auto before = sm::animation_assets_to_json(f.model.core().animation_data(f.character));
    auto replacements = delete_root_bone_replacements(f);
    require(f.model.replace_skeletons({f.skeleton}, refs(replacements)) == sm::result::success,
        "reference-bone deletion failed");
    require(f.model.core().topology().get<sm::node>(f.tip).has_value(), "effector unexpectedly disappeared");
    require(!action_exists(f.model.core().animation_data(f.character), action.id),
        "action survived deletion of its explicit Bone reference frame");
    f.model.undo();
    require(sm::animation_assets_to_json(f.model.core().animation_data(f.character)) == before,
        "undo did not restore IK pins/reference/path/easing/action identity exactly");
}

void unrelated_edit_preserves_action_and_does_not_confirm() {
    chain_fixture f;
    const auto action = f.add_rotation(f.root_bone);
    int confirmations = 0;
    f.model.set_topology_edit_confirmation([&](const auto&) { ++confirmations; return true; });
    auto replacements = delete_child_bone_replacements(f);
    require(f.model.replace_skeletons({f.skeleton}, refs(replacements)) == sm::result::success,
        "unrelated delete failed");
    require(action_exists(f.model.core().animation_data(f.character), action.id),
        "unrelated action was removed");
    require(confirmations == 0, "confirmation requested for an edit with no animation cascade");
}

void multiple_actions_and_order_are_preserved() {
    chain_fixture f;
    const auto first = f.add_rotation(f.root_bone, 0);
    const auto removed = f.add_rotation(f.child_bone, 100);
    const auto last = f.add_rotation(f.root_bone, 200);
    const auto second_removed = f.add_rotation(f.child_bone, 300);
    const auto before = sm::animation_assets_to_json(f.model.core().animation_data(f.character));
    int confirmations = 0;
    std::size_t warned_count = 0;
    f.model.set_topology_edit_confirmation([&](const auto& effects) {
        ++confirmations;
        warned_count = effects.removed_animation_actions.size();
        return true;
    });
    auto replacements = delete_child_bone_replacements(f);
    require(f.model.replace_skeletons({f.skeleton}, refs(replacements)) == sm::result::success,
        "multi-action delete failed");
    require(confirmations == 1 && warned_count == 2, "warning did not report all cascaded actions");
    const auto& actions = f.model.core().animation_data(f.character).animations.front().layers.front().actions;
    require(actions.size() == 2 && actions[0].id == first.id && actions[1].id == last.id,
        "unaffected action ordering changed");
    require(!action_exists(f.model.core().animation_data(f.character), removed.id) &&
            !action_exists(f.model.core().animation_data(f.character), second_removed.id),
        "not all dependent actions were removed");
    f.model.undo();
    require(sm::animation_assets_to_json(f.model.core().animation_data(f.character)) == before,
        "undo did not restore action IDs, ordering, timing, and payload exactly");
}

void regenerated_object_identity_cascades() {
    chain_fixture f;
    const auto action = f.add_ik(f.tip, {f.root});
    sm::topology replacements;
    auto copied = f.model.core().topology().skeleton(f.skeleton)->get().copy_to(replacements);
    require(copied.has_value(), "failed to copy regenerate fixture");
    const std::unordered_set<sm::object_id> regenerate{f.tip};
    auto preview = f.model.core().preview_replace_skeletons(
        {f.skeleton}, refs(replacements), regenerate);
    require(preview.has_value(), "regenerate preview failed");
    require(std::ranges::find(preview->removed_nodes, f.tip) != preview->removed_nodes.end(),
        "regenerated node identity was not reported as removed");
    require(preview->removed_animation_actions.size() == 1,
        "regenerated node did not cascade its dependent action");
    require(f.model.replace_skeletons({f.skeleton}, refs(replacements), regenerate) == sm::result::success,
        "regenerate replacement failed");
    require(!f.model.core().topology().get<sm::node>(f.tip), "regenerated node retained its old ID");
    require(!action_exists(f.model.core().animation_data(f.character), action.id),
        "action survived regeneration of a load-bearing node ID");
}

void skeleton_identity_is_not_heuristically_retargeted() {
    chain_fixture f;
    const auto action = f.add_rigid_translation(f.skeleton);
    auto replacements = delete_child_bone_replacements(f);
    auto preview = f.model.core().preview_replace_skeletons({f.skeleton}, refs(replacements));
    require(preview.has_value(), "split preview failed");
    require(std::ranges::find(preview->removed_skeletons, f.skeleton) != preview->removed_skeletons.end(),
        "split did not report replaced skeleton identity");
    require(f.model.replace_skeletons({f.skeleton}, refs(replacements)) == sm::result::success,
        "split failed");
    require(!action_exists(f.model.core().animation_data(f.character), action.id),
        "rigid translation was heuristically retargeted across split");
}

void merge_removes_old_skeleton_target_and_undo_restores() {
    mdl::project model;
    auto& a0 = model.core().create_skeleton({0.0, 0.0});
    auto& a1 = model.core().create_skeleton({10.0, 0.0});
    sm::node_ref ar(a0.root_node()), at(a1.root_node());
    auto ab = model.core().create_bone("a", *ar, *at);
    require(ab.has_value(), "failed first merge fixture bone");
    const auto first_skeleton = ab->get().owner().id();

    auto& b0 = model.core().create_skeleton({30.0, 0.0});
    auto& b1 = model.core().create_skeleton({40.0, 0.0});
    sm::node_ref br(b0.root_node()), bt(b1.root_node());
    auto bb = model.core().create_bone("b", *br, *bt);
    require(bb.has_value(), "failed second merge fixture bone");
    const auto second_skeleton = bb->get().owner().id();

    std::vector<sm::const_skel_ref> rig{
        *model.core().topology().skeleton(first_skeleton), *model.core().topology().skeleton(second_skeleton)};
    auto character = model.core().create_character(rig);
    require(character.has_value(), "failed merge fixture character");
    auto& assets = model.core().animation_data(character->get().id());
    sm::animation animation;
    animation.name = "merge";
    animation.base_pose = assets.default_pose;
    sm::animation_action action;
    sm::rigid_translation translation;
    translation.skeletons = {second_skeleton};
    translation.path = sm::motion_path(sm::line_path{{0,0},{1,0}});
    action.data = translation;
    animation.layers.push_back({{action}});
    assets.animations.push_back(animation);
    const auto before = sm::animation_assets_to_json(assets);

    int confirmations = 0;
    model.set_topology_edit_confirmation([&](const auto& effects) {
        ++confirmations;
        require(effects.removed_animation_actions.size() == 1, "merge warning count is wrong");
        return true;
    });
    require(model.add_bone(at->id(), br->id()) == sm::result::success, "merge add-bone failed");
    require(confirmations == 1, "merge did not request confirmation");
    require(!action_exists(model.core().animation_data(character->get().id()), action.id),
        "merge preserved action targeting destroyed skeleton ID");
    model.undo();
    require(model.core().topology().skeleton(second_skeleton).has_value(), "merge undo did not restore skeleton identity");
    require(sm::animation_assets_to_json(model.core().animation_data(character->get().id())) == before,
        "merge undo did not restore action exactly");
}

void cancellation_is_atomic() {
    chain_fixture f;
    f.add_rotation(f.child_bone);
    const auto before_topology = f.model.core().topology().to_json_str();
    const auto before_animation = sm::animation_assets_to_json(f.model.core().animation_data(f.character));
    int confirmations = 0;
    f.model.set_topology_edit_confirmation([&](const auto& effects) {
        ++confirmations;
        require(!effects.removed_animation_actions.empty(), "cancel warning had no cascade");
        return false;
    });
    auto replacements = delete_child_bone_replacements(f);
    require(f.model.replace_skeletons({f.skeleton}, refs(replacements)) == sm::result::cancelled,
        "cancelled edit did not report cancellation");
    require(confirmations == 1, "cancelled edit did not ask exactly once");
    require(f.model.core().topology().to_json_str() == before_topology, "cancel mutated topology");
    require(sm::animation_assets_to_json(f.model.core().animation_data(f.character)) == before_animation,
        "cancel mutated animation data");
    require(!f.model.can_undo(), "cancelled edit entered undo history");
}

void pose_membership_reconciliation_is_undoable() {
    chain_fixture f;
    auto& assets = f.model.core().animation_data(f.character);
    auto named = sm::capture_pose(f.model.core().topology(),
        f.model.core().character(f.character)->get().rig().skeleton_ids(), "Named");
    const auto named_id = named.id;
    assets.poses.push_back(named);
    const auto original_default = *assets.find_pose(assets.default_pose);
    const auto original_named = *assets.find_pose(named_id);

    // Default membership synchronization must add only the newly adopted node;
    // retained entries remain the authored Default positions rather than being
    // recaptured from whatever happens to be on screen at edit time.
    f.model.core().topology().get<sm::node>(f.root)->get().set_world_pos({123.0, 45.0});
    auto& extra = f.model.core().create_skeleton({60.0, 70.0});
    const auto extra_node = extra.root_node().id();
    std::vector<sm::const_skel_ref> adopted{extra};
    require(f.model.adopt_skeletons(f.character, adopted) == sm::result::success,
        "adopting a new component failed");

    const auto& after_adopt = f.model.core().animation_data(f.character);
    const auto* synced_default = after_adopt.find_pose(after_adopt.default_pose);
    const auto* incomplete_named = after_adopt.find_pose(named_id);
    require(synced_default && synced_default->node_positions.contains(extra_node),
        "Default pose did not acquire newly adopted node");
    require(synced_default->node_positions.at(f.root) == original_default.node_positions.at(f.root),
        "Default pose unnecessarily recaptured existing node positions");
    require(incomplete_named && !incomplete_named->node_positions.contains(extra_node),
        "named pose was silently extended instead of remaining explicitly incomplete");
    require(!sm::pose_compatible(*incomplete_named, f.model.core().topology(),
        f.model.core().character(f.character)->get().rig().skeleton_ids()),
        "new geometry did not make the named pose incomplete");

    f.model.undo();
    const auto& after_undo = f.model.core().animation_data(f.character);
    require(after_undo.find_pose(after_undo.default_pose)->node_positions == original_default.node_positions,
        "adoption undo did not restore Default pose exactly");
    require(after_undo.find_pose(named_id)->node_positions == original_named.node_positions,
        "adoption undo did not restore named pose exactly");

    require(f.model.redo() == sm::result::success, "adoption redo failed");
    require(f.model.core().animation_data(f.character).find_pose(
        f.model.core().animation_data(f.character).default_pose)->node_positions.contains(extra_node),
        "adoption redo did not restore synchronized Default pose");
}

void deleted_nodes_are_removed_from_named_poses() {
    chain_fixture f;
    auto& assets = f.model.core().animation_data(f.character);
    auto named = sm::capture_pose(f.model.core().topology(),
        f.model.core().character(f.character)->get().rig().skeleton_ids(), "Named");
    const auto named_id = named.id;
    assets.poses.push_back(named);
    const auto before = sm::animation_assets_to_json(assets);

    auto replacements = delete_tip_replacements(f);
    require(f.model.replace_skeletons({f.skeleton}, refs(replacements)) == sm::result::success,
        "tip deletion failed");
    const auto& after = f.model.core().animation_data(f.character);
    const auto* default_pose = after.find_pose(after.default_pose);
    const auto* named_pose = after.find_pose(named_id);
    require(default_pose && !default_pose->node_positions.contains(f.tip),
        "Default pose retained a deleted node");
    require(named_pose && !named_pose->node_positions.contains(f.tip),
        "named pose retained a genuinely deleted node");
    require(sm::pose_compatible(*named_pose, f.model.core().topology(),
        f.model.core().character(f.character)->get().rig().skeleton_ids()),
        "named pose did not become compatible after deleted-node cleanup");

    f.model.undo();
    require(sm::animation_assets_to_json(f.model.core().animation_data(f.character)) == before,
        "deletion undo did not restore removed named-pose entries");
}

} // namespace

int main() {
    try {
        dependency_enumerator_is_complete();
        validation_rejects_unresolved_action_reference();
        bone_target_delete_undo_redo_exact();
        ik_effector_and_pin_cascade();
        bone_reference_only_cascades();
        unrelated_edit_preserves_action_and_does_not_confirm();
        multiple_actions_and_order_are_preserved();
        regenerated_object_identity_cascades();
        skeleton_identity_is_not_heuristically_retargeted();
        merge_removes_old_skeleton_target_and_undo_restores();
        cancellation_is_atomic();
        pose_membership_reconciliation_is_undoable();
        deleted_nodes_are_removed_from_named_poses();
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
    return 0;
}
