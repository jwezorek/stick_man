#include "commands.hpp"
#include <ranges>
#include <unordered_set>
#include <cassert>

/*------------------------------------------------------------------------------------------------*/

namespace r = std::ranges;
namespace rv = std::ranges::views;
namespace {
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
mdl::command mdl::commands::make_create_node_command(const std::string& canvas,
        const sm::point& pt, const std::string& node_name) {
    auto state = std::make_shared<create_node_state>();
    state->canvas_name = canvas;
    state->node_name = node_name;
    state->loc = pt;
    return {
        [state](mdl::project& proj) {
            sm::skeleton* skel = nullptr;
            if (state->snapshot.empty()) {
                auto& created = proj.world_.create_skeleton(state->loc);
                created.set_name(created.root_node(), state->node_name);
                state->skeleton = created.id();
                created.copy_to(state->snapshot);
                skel = &created;
            } else {
                auto snapshot = state->snapshot.skeleton(state->skeleton);
                auto restored = snapshot->get().copy_to(proj.world_);
                if (!restored) {
                    throw std::runtime_error("unable to restore created skeleton");
                }
                skel = &restored->get();
            }
            proj.tabs_[state->canvas_name].push_back(state->skeleton);
            emit proj.new_skeleton_added(state->canvas_name, *skel);
        },
        [state](mdl::project& proj) {
            proj.delete_skeleton_from_canvas_table(state->canvas_name, state->skeleton);
            proj.world_.delete_skeleton(state->skeleton);
            emit proj.refresh_canvas(proj, state->canvas_name, true);
        }
    };
}
mdl::commands::add_bone_state::add_bone_state(const std::string& str,
        const std::string& name, const handle& u, const handle& v):
    canvas_name(str), bone_name(name), u_hnd(u), v_hnd(v), bone_id(sm::object_id::generate()) {
}
mdl::command mdl::commands::make_add_bone_command(const std::string& tab,
        const handle& u_hnd, const handle& v_hnd, const std::string& bone_name) {
    auto state = std::make_shared<add_bone_state>(tab, bone_name, u_hnd, v_hnd);
    return {
        [state](mdl::project& proj) {
            auto& u = state->u_hnd.to<sm::node>(proj.world_);
            auto& v = state->v_hnd.to<sm::node>(proj.world_);

            if (&u.owner() == &v.owner()) {
                return;
            }
            auto& skel_u = u.owner();
            auto& skel_v = v.owner();
            auto new_u = skel_u.copy_to(state->original);
            auto new_v = skel_v.copy_to(state->original);
            if (!new_u || !new_v) {
                throw std::runtime_error("skeleton copy failed");
            }
            emit proj.pre_new_bone_added(u, v);
            proj.delete_skeleton_from_canvas_table(state->canvas_name, v.owner().id());
            auto bone = proj.world_.create_bone(state->bone_id, state->bone_name, u, v);
            if (!bone) {
                throw std::runtime_error("create_bone failed");
            }
            state->merged = bone->get().owner().id();
            emit proj.new_bone_added(bone->get());
        },
        [state](mdl::project& proj) {
            proj.replace_skeletons_aux(
                state->canvas_name,
                {state->merged},
                state->original.skeletons() | r::to<std::vector<sm::skel_ref>>(),
                nullptr
            );
            state->original.clear();
        }
    };
}
mdl::commands::replace_skeleton_state::replace_skeleton_state(
        const std::string& canv,
        const std::vector<sm::object_id>& replacees_arg,
        const std::vector<sm::skel_ref>& replacers):
    canvas_name(canv), replacee_ids(replacees_arg) {
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
        const std::string& canvas_name,
        const std::vector<sm::object_id>& replacees,
        const std::vector<sm::skel_ref>& replacements) {
    auto state = std::make_shared<replace_skeleton_state>(canvas_name, replacees, replacements);
    return {
        [state](mdl::project& proj) {
            if (state->replacees.empty()) {
                for (const auto& skel_id : state->replacee_ids) {
                    auto skel = proj.world_.skeleton(skel_id);
                    if (!skel || !skel->get().copy_to(state->replacees)) {
                        throw std::runtime_error("unable to snapshot replaced skeleton");
                    }
                }
            }
            state->replacement_ids.clear();
            proj.replace_skeletons_aux(
                state->canvas_name,
                state->replacee_ids,
                state->replacements.skeletons() | r::to<std::vector<sm::skel_ref>>(),
                &state->replacement_ids
            );
        },
        [state](mdl::project& proj) {
            proj.replace_skeletons_aux(
                state->canvas_name,
                state->replacement_ids,
                state->replacees.skeletons() | r::to<std::vector<sm::skel_ref>>(),
                nullptr
            );
        }
    };
}
mdl::commands::transform_nodes_and_bones_state::transform_nodes_and_bones_state(
        project& proj, const std::vector<handle>& node_hnds,
        const std::function<void(sm::node&)>& fn):
    transform_nodes{fn} {
    for (auto hnd : node_hnds) {
        auto& node = hnd.to<sm::node>(proj.world_);
        if (canvas.empty()) {
            canvas = proj.canvas_name_from_skeleton(node.owner().id());
        }
        nodes.push_back(hnd);
        old_node_to_position[hnd] = node.world_pos();
    }
}
mdl::commands::transform_nodes_and_bones_state::transform_nodes_and_bones_state(
        project& proj,
        const std::vector<handle>& bone_hnds,
        const std::function<void(sm::bone&)>& fn):
    transform_bones{fn} {

    std::unordered_set<sm::node*> node_set;
    for (auto hnd : bone_hnds) {
        auto& bone = hnd.to<sm::bone>(proj.world_);
        if (canvas.empty()) {
            canvas = proj.canvas_name_from_skeleton(bone.owner().id());
        }
        bones.push_back(hnd);
        if (auto rot_con = bone.rotation_constraint()) {
            old_bone_to_rotcon[hnd] = *rot_con;
        }
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
            if (state->transform_nodes) {
                for (auto node_hnd : state->nodes) {
                    state->transform_nodes(node_hnd.to<sm::node>(proj.world_));
                }
            } else if (state->transform_bones) {
                for (auto bone_hnd : state->bones) {
                    state->transform_bones(bone_hnd.to<sm::bone>(proj.world_));
                }
            } else {
                throw std::runtime_error("bad call to make_transform_bones_or_nodes_command");
            }
            emit proj.refresh_canvas(proj, state->canvas, false);
        },
        [state](project& proj) {
            for (auto node_hnd : state->nodes) {
                auto& node = node_hnd.to<sm::node>(proj.world_);
                node.set_world_pos(state->old_node_to_position[node_hnd]);
            }
            for (auto bone_hnd : state->bones) {
                auto& bone = bone_hnd.to<sm::bone>(proj.world_);
                if (state->old_bone_to_rotcon.contains(bone_hnd)) {
                    auto rot_con = state->old_bone_to_rotcon[bone_hnd];
                    bone.set_rotation_constraint(
                        rot_con.start_angle, rot_con.span_angle, rot_con.relative_to_parent);
                } else if (state->old_bone_to_rotcon.empty() && bone.rotation_constraint()) {
                    bone.remove_rotation_constraint();
                }
            }
            emit proj.refresh_canvas(proj, state->canvas, false);
        }
    };
}
mdl::command mdl::commands::make_transform_node_positions_command(
        project& proj,
        const std::vector<std::tuple<handle, sm::point>>& old_locs,
        const std::vector<std::tuple<handle, sm::point>>& new_locs) {
    return {
        [new_locs](project& proj) {
            std::string canvas;
            for (const auto& [node_hnd, loc] : new_locs) {
                auto& node = node_hnd.to<sm::node>(proj.world_);
                if (canvas.empty()) {
                    canvas = proj.canvas_name_from_skeleton(node.owner().id());
                }
                node.set_world_pos(loc);
            }
            emit proj.refresh_canvas(proj, canvas, false);
        },
        [old_locs](project& proj) {
            std::string canvas;
            for (const auto& [node_hnd, loc] : old_locs) {
                auto& node = node_hnd.to<sm::node>(proj.world_);
                if (canvas.empty()) {
                    canvas = proj.canvas_name_from_skeleton(node.owner().id());
                }
                node.set_world_pos(loc);
            }
            emit proj.refresh_canvas(proj, canvas, false);
        }
    };
}
mdl::command mdl::commands::make_add_tab_command(const std::string& tab) {
    auto tab_name = std::make_shared<std::string>(tab);
    return {
        [tab_name](project& proj) {
            proj.tabs_[*tab_name] = {};
            emit proj.tab_created_or_deleted(*tab_name, true);
        },
        [tab_name](project& proj) {
            proj.tabs_.erase(*tab_name);
            emit proj.tab_created_or_deleted(*tab_name, false);
        }
    };
}
