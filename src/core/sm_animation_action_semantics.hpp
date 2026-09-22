#pragma once

#include "sm_animation.hpp"

#include <concepts>
#include <type_traits>
#include <utility>

namespace sm::detail {

// Enumerate every persistent project-object ID physically stored by an action.
// `active_dependency` distinguishes IDs that currently participate in the
// action's semantics from dormant storage such as reference_bone when the
// translation reference is not Bone. Remapping must visit both; dependency
// consumers normally use only active entries.
//
// These overloads are deliberately exhaustive over action_data. Callers use a
// generic std::visit that has no fallback, so adding an action_data alternative
// without defining its persistent-reference semantics is a compile-time error.
template<class Action, class Visitor>
    requires std::same_as<std::remove_cvref_t<Action>, rigid_rotation>
void for_each_action_persistent_reference(Action&& action, Visitor&& visit) {
    std::forward<Visitor>(visit)(animation_dependency_kind::bone, action.bone, true);
}

template<class Action, class Visitor>
    requires std::same_as<std::remove_cvref_t<Action>, ik_rotation>
void for_each_action_persistent_reference(Action&& action, Visitor&& visit) {
    auto&& v = visit;
    v(animation_dependency_kind::node, action.effector, true);
    v(animation_dependency_kind::node, action.pivot_node, true);
}

template<class Action, class Visitor>
    requires std::same_as<std::remove_cvref_t<Action>, rigid_translation>
void for_each_action_persistent_reference(Action&& action, Visitor&& visit) {
    auto&& v = visit;
    for (auto&& id : action.skeletons)
        v(animation_dependency_kind::skeleton, id, true);
    v(animation_dependency_kind::bone, action.reference_bone,
        action.reference == translation_reference::bone);
}

template<class Action, class Visitor>
    requires std::same_as<std::remove_cvref_t<Action>, ik_translation>
void for_each_action_persistent_reference(Action&& action, Visitor&& visit) {
    auto&& v = visit;
    v(animation_dependency_kind::node, action.effector, true);
    for (auto&& id : action.pins)
        v(animation_dependency_kind::node, id, true);
    v(animation_dependency_kind::bone, action.reference_bone,
        action.reference == translation_reference::bone);
}

} // namespace sm::detail
