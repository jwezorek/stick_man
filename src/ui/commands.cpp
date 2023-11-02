#include "commands.h"
#include <boost/functional/hash.hpp>
#include <ranges>
#include <unordered_set>
#include <cassert>

/*------------------------------------------------------------------------------------------------*/

namespace r = std::ranges;
namespace rv = std::ranges::views;

namespace {
    template<class... Ts> struct overload : Ts... { using Ts::operator()...; };
}

sm::expected_skel ui::commands::skel_from_handle(ui::project& proj, const skel_piece_handle& hnd) {
    return proj.world_.skeleton(hnd.skel_name);
}

ui::commands::skel_piece_handle ui::commands::to_handle(const ui::skel_piece& piece) {
    return std::visit(
        overload{
            [](sm::is_node_or_bone_ref auto node_or_bone)->skel_piece_handle {
                auto skel_name = node_or_bone.get().owner().get().name();
                return { skel_name, node_or_bone.get().name() };
            } ,
            [](sm::skel_ref itm)->skel_piece_handle {
                auto& skel = itm.get();
                return {skel.name(), {}};
            }
        },
        piece
    );
}

ui::command ui::commands::make_create_node_command(const std::string& tab, const sm::point& pt) {
    auto state = std::make_shared<create_node_state>(tab, "", pt);

    return {
        [state](ui::project& proj) {
            auto skel = proj.world_.create_skeleton(state->loc);
            state->skeleton = skel.get().name();
            proj.tabs_[state->tab_name].push_back(skel.get().name());
            emit proj.new_skeleton_added(skel);
        },
        [state](ui::project& proj) {
            proj.delete_skeleton_name_from_canvas_table(
                state->tab_name, state->skeleton
            );
            proj.world_.delete_skeleton(state->skeleton);
            emit proj.refresh_canvas(proj, state->tab_name, true);
        }
    };
}

ui::commands::add_bone_state::add_bone_state(const std::string& cn, sm::node& u, sm::node& v) :
    canvas_name(cn), u_hnd(to_handle(u)), v_hnd(to_handle(v))
{}

ui::command ui::commands::make_add_bone_command(const std::string& tab, sm::node& u, sm::node& v) {
    auto state = std::make_shared<add_bone_state>(tab, u, v);
    return {
        [state](ui::project& proj) {
            auto& u = from_handle<sm::node>(proj, state->u_hnd);
            auto& v = from_handle<sm::node>(proj, state->v_hnd);

            if (&u.owner().get() == &v.owner().get()) {
                return;
            }

            auto& skel_u = u.owner().get();
            auto& skel_v = v.owner().get();
            auto new_u = skel_u.copy_to(state->original);
            auto new_v = skel_v.copy_to(state->original);
            assert("skeleton copy failed", new_u && new_v);

            emit proj.pre_new_bone_added(u, v);
            proj.delete_skeleton_name_from_canvas_table(
                state->canvas_name, v.owner().get().name()
            );

            auto bone = proj.world_.create_bone({}, u, v);
            assert("create_bone failed", false);

            state->merged = bone->get().owner().get().name();
            emit proj.new_bone_added(bone->get());
        },
        [state](ui::project& proj) {
            proj.replace_skeletons_aux(state->canvas_name,
                {state->merged},
                state->original.skeletons() | r::to<std::vector<sm::skel_ref>>(),
                nullptr
            );
            state->original.clear();
        }
    };
}

ui::commands::replace_skeleton_state::replace_skeleton_state(const std::string& canv, 
        const std::vector<std::string>& replacees, const std::vector<sm::skel_ref>& replacers):
        canvas_name(canv),
        replacee_names(replacees) {
    for (auto skel : replacers) {
        skel.get().copy_to(replacements);
    }
}

ui::command ui::commands::make_replace_skeletons_command(
        const std::string& canvas_name,
        const std::vector<std::string>& replacees,
        const std::vector<sm::skel_ref>& replacements) {
    auto state = std::make_shared<replace_skeleton_state>(canvas_name, replacees, replacements);
    return {
        [state](ui::project& proj) {
            if (state->replacees.empty()) {
                for (const auto& skel_name : state->replacee_names) {
                    proj.world_.skeleton(skel_name)->get().copy_to(state->replacees);
                }
            }
            state->replacement_names.clear();
            proj.replace_skeletons_aux(
                state->canvas_name,
                state->replacee_names,
                state->replacements.skeletons() | r::to<std::vector<sm::skel_ref>>(),
                &state->replacement_names
            );
        },
        [state](ui::project& proj) {
            proj.replace_skeletons_aux(
                state->canvas_name,
                state->replacement_names,
                state->replacees.skeletons() | r::to<std::vector<sm::skel_ref>>(),
                nullptr
            );
        }
    };
}

ui::commands::trasform_nodes_and_bones_state::trasform_nodes_and_bones_state(
        const std::string& canvas_name,
        const std::vector<sm::node_ref>& node_refs,
        const std::function<void(sm::node&)>& fn) :
        canvas{ canvas_name },
        transform_nodes{ fn } {
    for (auto node_ref : node_refs) {
        auto& node = node_ref.get();
        auto handle = to_handle(node_ref);
        nodes.push_back(handle);
        old_node_to_position[handle] = node.world_pos();
    }
}

ui::commands::trasform_nodes_and_bones_state::trasform_nodes_and_bones_state(
        const std::string& canvas_name,
        const std::vector<sm::bone_ref>& bone_refs,
        const std::function<void(sm::bone&)>& fn) :
        canvas{ canvas_name },
        transform_bones{ fn } {

    std::unordered_set<sm::node*> node_set;
    for (auto bone_ref : bone_refs) {
        auto& bone = bone_ref.get();
        auto handle = to_handle(bone_ref);
        bones.push_back(handle);

        auto rot_con = bone.rotation_constraint();
        if (rot_con) {
            old_bone_to_rotcon[handle] = *rot_con;
        }

        node_set.insert(&bone.parent_node());
        node_set.insert(&bone.child_node());
    }

    for (auto* node_ptr : node_set) {
        auto& node = *node_ptr;
        auto handle = to_handle( std::ref(node) );
        nodes.push_back(handle);
        old_node_to_position[handle] = node.world_pos();
    }

}

ui::command ui::commands::make_transform_bones_or_nodes_command(
        const std::string& canv,
        const std::vector<sm::node_ref>& nodes,
        const std::vector<sm::bone_ref>& bones,
        const std::function<void(sm::node&)>& nodes_fn,
        const std::function<void(sm::bone&)>& bones_fn) {

    std::shared_ptr<trasform_nodes_and_bones_state> state = (nodes_fn) ?
        std::make_shared<trasform_nodes_and_bones_state>(canv, nodes, nodes_fn) :
        std::make_shared<trasform_nodes_and_bones_state>(canv, bones, bones_fn);
    return {
        [state](project& proj) {
            // either there is a node transformation function or
            // a bone function but not both and not neither.
            if (state->transform_nodes) {
                for (auto node_hnd : state->nodes) {
                    auto& node = from_handle<sm::node>(proj, node_hnd);
                    state->transform_nodes(node);
                }
            } else if (state->transform_bones) {
                for (auto bone_hnd : state->bones) {
                    auto& bone = from_handle<sm::bone>(proj, bone_hnd);
                    state->transform_bones(bone);
                }
            } else {
                assert("bad call to make_transform_bones_or_nodes_command", false);
            }
            emit proj.refresh_canvas(proj, state->canvas, false);
        },
        [state](project& proj) {
            for (auto node_hnd : state->nodes) {
                auto& node = from_handle<sm::node>(proj, node_hnd);
                node.set_world_pos(state->old_node_to_position[node_hnd]);
            }
            for (auto bone_hnd : state->bones) {
                auto& bone = from_handle<sm::bone>(proj, bone_hnd);
                auto rot_con = state->old_bone_to_rotcon[bone_hnd];
                bone.set_rotation_constraint(
                    rot_con.start_angle, rot_con.span_angle, rot_con.relative_to_parent
                );
            }
            emit proj.refresh_canvas(proj, state->canvas, false);
        }
    };
}

bool ui::commands::skel_piece_handle::operator==(const skel_piece_handle& hand) const
{
    return this->skel_name == hand.skel_name && this->piece_name == hand.piece_name;
}

size_t ui::commands::handle_hash::operator()(const skel_piece_handle& hand) const
{
    size_t seed = 0;
    boost::hash_combine(seed, hand.skel_name);
    boost::hash_combine(seed, hand.piece_name);
    return seed;
}
