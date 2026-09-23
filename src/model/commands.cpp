#include "commands.hpp"
#include "core/sm_geometry_batch.hpp"
#include <ranges>
#include <unordered_set>
#include <cassert>
#include <stdexcept>
#include <utility>

/*------------------------------------------------------------------------------------------------*/
namespace r = std::ranges;
namespace rv = std::ranges::views;
namespace {
    std::vector<sm::object_id> cascade_characters(const sm::topology_edit_effects& effects) {
        std::vector<sm::object_id> ids;
        for (const auto& removed : effects.removed_animation_actions) ids.push_back(removed.character);
        std::ranges::sort(ids);
        ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
        return ids;
    }

    auto find_roots(const std::unordered_set<sm::node*>& node_set) {
        return rv::all(node_set) |
            rv::filter(
                [&node_set](const auto* node_ptr)->bool {
                    auto parent = node_ptr->parent_bone();
                    if (!parent) {
                        return true;
                    }
                    sm::node& parent_node = const_cast<sm::node&>(parent->get().parent_node());
                    return !node_set.contains(&parent_node);
                }
            );
    }
    std::unordered_set<sm::node*> downstream_envelope(const std::unordered_set<sm::node*>& node_set) {
        std::unordered_set<sm::node*> envelope;
        for (auto* root : find_roots(node_set)) {
            sm::visit_nodes(*root,
                [&envelope](sm::node& node)->sm::visit_result {
                    envelope.insert(&node);
                    return sm::visit_result::continue_traversal;
                }
            );
        }
        return envelope;
    }
}
mdl::command mdl::commands::make_create_node_command(
        const sm::point& pt, const std::string& node_name) {
    auto state = std::make_shared<create_node_state>();
    state->node_name = node_name;
    state->loc = pt;
    return {
        [state](mdl::project& proj) {
            sm::skeleton* skel = nullptr;
            if (state->snapshot.empty()) {
                auto& created = proj.core().create_skeleton(state->loc);
                proj.core().rename(created.root_node().id(), state->node_name);
                state->skeleton = created.id();
                if (!created.copy_to(state->snapshot)) {
                    throw std::runtime_error("unable to snapshot created skeleton");
                }
                skel = &created;
            } else {
                auto snapshot = state->snapshot.skeleton(state->skeleton);
                auto restored = proj.core().copy_skeleton(snapshot->get());
                if (!restored) {
                    throw std::runtime_error("unable to restore created skeleton");
                }
                skel = &restored->get();
            }
            emit proj.new_skeleton_added(*skel);
        },
        [state](mdl::project& proj) {
            proj.core().delete_skeleton(state->skeleton);
            emit proj.refresh_canvas(proj, true);
        }
    };
}
mdl::commands::add_bone_state::add_bone_state(
        const std::string& name, const handle& u, const handle& v,
        const sm::topology_edit_effects& effects):
    bone_name(name), u_hnd(u), v_hnd(v), cascade_characters(::cascade_characters(effects)) {
}
mdl::command mdl::commands::make_add_bone_command(
        const handle& u_hnd, const handle& v_hnd, const std::string& bone_name,
        const sm::topology_edit_effects& effects) {
    auto state = std::make_shared<add_bone_state>(bone_name, u_hnd, v_hnd, effects);
    return {
        [state](mdl::project& proj) {
            auto& u = commands::resolve<sm::node>(proj, state->u_hnd);
            auto& v = commands::resolve<sm::node>(proj, state->v_hnd);
            state->status = proj.core().can_create_bone(u, v);
            if (state->status != sm::result::success) return;
            auto& skel_u = u.owner();
            auto& skel_v = v.owner();
            state->before_constraints = proj.core().constraints();
            state->membership = proj.core().snapshot_membership(
                {skel_u.id(), skel_v.id()}, state->cascade_characters);
            auto new_u = skel_u.copy_to(state->original);
            auto new_v = skel_v.copy_to(state->original);
            if (!new_u || !new_v) {
                throw std::runtime_error("skeleton copy failed");
            }
            emit proj.pre_new_bone_added(u, v);
            auto bone = state->bone_id
                ? proj.core().create_bone(*state->bone_id, state->bone_name, u, v)
                : proj.core().create_bone(state->bone_name, u, v);
            if (!bone) {
                state->status = bone.error();
                state->original.clear();
                return;
            }
            if (!state->bone_id) {
                state->bone_id = bone->get().id();
            }
            state->merged = bone->get().owner().id();
            if (state->after_constraints) {
                if (proj.core().restore_constraints(*state->after_constraints) != sm::result::success)
                    throw std::runtime_error("unable to restore merged constraints");
            } else state->after_constraints = proj.core().constraints();
            emit proj.new_bone_added(bone->get());
        },
        [state](mdl::project& proj) {
            proj.replace_skeletons_aux(
                {state->merged},
                state->original.skeletons() | r::to<std::vector<sm::skel_ref>>(),
                {}, &state->membership
            );
            state->original.clear();
            if (proj.core().restore_constraints(state->before_constraints) != sm::result::success)
                throw std::runtime_error("unable to restore pre-merge constraints");
            emit proj.refresh_canvas(proj, false);
        },
        [state] { return state->status; }
    };
}
mdl::commands::replace_skeleton_state::replace_skeleton_state(
        const std::vector<sm::object_id>& replacees_arg,
        const std::vector<sm::skel_ref>& replacers,
        const std::unordered_set<sm::object_id>& regenerate_ids_arg,
        const sm::topology_edit_effects& effects):
    replacee_ids(replacees_arg), regenerate_ids(regenerate_ids_arg),
    cascade_characters(::cascade_characters(effects)) {
    for (auto skel : replacers) {
        // An insertion (not a replacement) is an editor duplication operation, e.g. paste.
        // Allocate the duplicate IDs once here; redo then restores those same IDs.
        auto result = replacee_ids.empty()
            ? skel->duplicate_to(replacements)
            : skel->copy_to(replacements);
        if (!result) {
            throw std::runtime_error("unable to snapshot replacement skeleton");
        }
    }
}
mdl::command mdl::commands::make_replace_skeletons_command(
        const std::vector<sm::object_id>& replacees,
        const std::vector<sm::skel_ref>& replacements,
        const std::unordered_set<sm::object_id>& regenerate_ids,
        const sm::topology_edit_effects& effects) {
    auto state = std::make_shared<replace_skeleton_state>(
        replacees, replacements, regenerate_ids, effects);
    return {
        [state](mdl::project& proj) {
            state->before_membership = proj.core().snapshot_membership(
                state->replacee_ids, state->cascade_characters);
            state->before_constraints = proj.core().constraints();
            if (state->replacees.empty()) {
                for (const auto& skel_id : state->replacee_ids) {
                    auto skel = proj.topology().skeleton(skel_id);
                    if (!skel || !skel->get().copy_to(state->replacees)) {
                        throw std::runtime_error("unable to snapshot replaced skeleton");
                    }
                }
            }
            auto change = proj.replace_skeletons_aux(
                state->replacee_ids,
                state->replacements.skeletons() | r::to<std::vector<sm::skel_ref>>(),
                state->regenerate_ids,
                state->after_membership ? &*state->after_membership : nullptr
            );
            state->status = change.status;
            if (change.status != sm::result::success) return;
            if (state->after_constraints) {
                if (proj.core().restore_constraints(*state->after_constraints) != sm::result::success)
                    throw std::runtime_error("unable to restore replacement constraints");
            } else state->after_constraints = proj.core().constraints();
            state->replacement_ids = std::move(change.added_skeleton_ids);
            state->after_membership = proj.core().snapshot_membership(
                state->replacement_ids, state->cascade_characters);
            // Replacement may remap any object ID to avoid collisions. Retain the
            // actual inserted topology so later commands keep valid handles on redo.
            sm::topology inserted;
            for (const auto& id : state->replacement_ids) {
                auto skel = proj.topology().skeleton(id);
                if (!skel || !skel->get().copy_to(inserted)) {
                    throw std::runtime_error("unable to snapshot inserted skeleton");
                }
            }
            state->replacements = std::move(inserted);
            state->regenerate_ids.clear();
        },
        [state](mdl::project& proj) {
            proj.replace_skeletons_aux(
                state->replacement_ids,
                state->replacees.skeletons() | r::to<std::vector<sm::skel_ref>>(),
                {}, &state->before_membership
            );
            if (proj.core().restore_constraints(state->before_constraints) != sm::result::success)
                throw std::runtime_error("unable to restore replaced constraints");
            emit proj.refresh_canvas(proj, false);
        },
        [state] { return state->status; }
    };
}
mdl::commands::transform_nodes_and_bones_state::transform_nodes_and_bones_state(
        project& proj, const std::vector<handle>& node_hnds,
        const std::function<void(sm::node&)>& fn):
    transform_nodes{fn}, old_constraints(proj.core().constraints()) {
    for (auto hnd : node_hnds) {
        auto& node = commands::resolve<sm::node>(proj, hnd);
        nodes.push_back(hnd);
        old_node_to_position[hnd] = node.world_pos();
    }
    // A constrained node edit can move rigid siblings and their descendants.
    for (auto skel : proj.topology().skeletons()) for (auto node : skel->nodes())
        old_node_to_position[to_handle(node)] = node->world_pos();
}
mdl::commands::transform_nodes_and_bones_state::transform_nodes_and_bones_state(
        project& proj,
        const std::vector<handle>& bone_hnds,
        const std::function<void(sm::bone&)>& fn):
    transform_bones{fn}, old_constraints(proj.core().constraints()) {
    std::unordered_set<sm::node*> node_set;
    for (auto hnd : bone_hnds) {
        auto& bone = commands::resolve<sm::bone>(proj, hnd);
        bones.push_back(hnd);
        node_set.insert(&bone.parent_node());
    }
    for (auto* node_ptr : downstream_envelope(node_set)) {
        auto& node = *node_ptr;
        auto hnd = to_handle(sm::ref(node));
        nodes.push_back(hnd);
        old_node_to_position[hnd] = node.world_pos();
    }
}
mdl::command mdl::commands::make_transform_bones_or_nodes_command(
        project& proj,
        const std::vector<handle>& nodes,
        const std::vector<handle>& bones,
        const std::function<void(sm::node&)>& nodes_fn,
        const std::function<void(sm::bone&)>& bones_fn) {
    std::shared_ptr<transform_nodes_and_bones_state> state = nodes_fn
        ? std::make_shared<transform_nodes_and_bones_state>(proj, nodes, nodes_fn)
        : std::make_shared<transform_nodes_and_bones_state>(proj, bones, bones_fn);
    return {
        [state](project& proj) {
            if (state->new_constraints) {
                sm::geometry_batch batch(proj.core().topology());
                for (const auto& [handle, point] : state->new_node_to_position)
                    commands::resolve<sm::node>(proj, handle).set_world_pos(point);
                if (proj.core().restore_constraints(*state->new_constraints) != sm::result::success)
                    throw std::runtime_error("unable to restore transformed constraints");
                if (batch.commit() != sm::result::success) throw std::runtime_error("invalid restored geometry");
                emit proj.refresh_canvas(proj, false);
                return;
            }
            if (state->transform_nodes) {
                for (auto node_hnd : state->nodes) {
                    state->transform_nodes(commands::resolve<sm::node>(proj, node_hnd));
                }
            } else if (state->transform_bones) {
                for (auto bone_hnd : state->bones) {
                    state->transform_bones(commands::resolve<sm::bone>(proj, bone_hnd));
                }
            } else {
                throw std::runtime_error("bad call to make_transform_bones_or_nodes_command");
            }
            state->new_constraints = proj.core().constraints();
            for (const auto& [node_hnd, old_position] : state->old_node_to_position)
                state->new_node_to_position[node_hnd] = commands::resolve<sm::node>(proj, node_hnd).world_pos();
            emit proj.refresh_canvas(proj, false);
        },
        [state](project& proj) {
            sm::geometry_batch batch(proj.core().topology());
            for (const auto& [node_hnd, old_position] : state->old_node_to_position) {
                auto& node = commands::resolve<sm::node>(proj, node_hnd);
                node.set_world_pos(old_position);
            }
            if (proj.core().restore_constraints(state->old_constraints) != sm::result::success)
                throw std::runtime_error("unable to restore original constraints");
            if (batch.commit() != sm::result::success) throw std::runtime_error("invalid restored geometry");
            emit proj.refresh_canvas(proj, false);
        }
    };
}
mdl::command mdl::commands::make_transform_node_positions_command(
        project& proj,
        const std::vector<std::tuple<handle, sm::point>>& old_locs,
        const std::vector<std::tuple<handle, sm::point>>& new_locs) {
    return {
        [new_locs](project& proj) {
            sm::geometry_batch batch(proj.core().topology());
            for (const auto& [node_hnd, loc] : new_locs) {
                auto& node = commands::resolve<sm::node>(proj, node_hnd);
                node.set_world_pos(loc);
            }
            if (batch.commit() != sm::result::success) throw std::runtime_error("invalid restored geometry");
            emit proj.refresh_canvas(proj, false);
        },
        [old_locs](project& proj) {
            sm::geometry_batch batch(proj.core().topology());
            for (const auto& [node_hnd, loc] : old_locs) {
                auto& node = commands::resolve<sm::node>(proj, node_hnd);
                node.set_world_pos(loc);
            }
            if (batch.commit() != sm::result::success) throw std::runtime_error("invalid restored geometry");
            emit proj.refresh_canvas(proj, false);
        }
    };
}
