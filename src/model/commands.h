#pragma once

#include <vector>
#include <string>
#include <expected>
#include <variant>
#include "../core/sm_types.h"
#include "../core/sm_visit.h"
#include "project.h"
#include <unordered_map>

/*------------------------------------------------------------------------------------------------*/

namespace mdl {

    class commands {
        friend class project;
    private:

        template<typename T>
        using handle_table = std::unordered_map<handle, T, handle_hash>;

        struct create_node_state {
            std::string canvas_name;
            sm::object_id skeleton;
            sm::point loc;
            sm::world snapshot;
        };

        struct add_bone_state {
            std::string canvas_name;
            handle u_hnd;
            handle v_hnd;
            sm::world original;
            sm::object_id merged;
            sm::object_id bone_id;

            add_bone_state(const std::string& str,
                const handle& u_hnd,
                const handle& v_hnd);
        };

        struct rename_state {
            handle object;
            std::string old_name;
            std::string new_name;
        };

        struct replace_skeleton_state {
            std::string canvas_name;
            std::vector<sm::object_id> replacee_ids;
            sm::world replacees;
            std::vector<sm::object_id> replacement_ids;
            sm::world replacements;

            replace_skeleton_state(const std::string& canv,
                const std::vector<sm::object_id>& replacees,
                const std::vector<sm::skel_ref>& replacements);
        };

        struct transform_nodes_and_bones_state {
            std::string canvas;
            std::function<void(sm::node&)> transform_nodes;
            std::function<void(sm::bone&)> transform_bones;
            std::vector<handle> nodes;
            std::vector<handle> bones;
            handle_table<sm::point> old_node_to_position;
            handle_table<sm::rot_constraint> old_bone_to_rotcon;

            transform_nodes_and_bones_state(
                project& proj,
                const std::vector<handle>& nodes,
                const std::function<void(sm::node&)>& fn
            );

            transform_nodes_and_bones_state(
                project& proj,
                const std::vector<handle>& bones,
                const std::function<void(sm::bone&)>& fn
            );
        };

        template<sm::is_skel_piece T>
        static void rename(project& proj, const handle& hnd, const std::string& name) {
            auto& obj = hnd.to<T>(proj.world_);
            proj.rename_aux(sm::ref(obj), name);
        }

        template<sm::is_skel_piece T>
        static command make_rename_command(sm::ref<T> piece, const std::string& new_name) {
            auto state = std::make_shared<rename_state>(
                to_handle(skel_piece{piece}), piece->name(), new_name);

            return {
                [state](mdl::project& proj) {
                    rename<T>(proj, state->object, state->new_name);
                },
                [state](mdl::project& proj) {
                    rename<T>(proj, state->object, state->old_name);
                }
            };
        }

        static command make_create_node_command(const std::string& tab, const sm::point& pt);
        static command make_add_bone_command(const std::string& tab,
            const handle& u_hnd, const handle& v_hnd);
        static command make_replace_skeletons_command(
            const std::string& canvas_name,
            const std::vector<sm::object_id>& replacees,
            const std::vector<sm::skel_ref>& replacements
        );
        static command make_transform_bones_or_nodes_command(
            project& proj,
            const std::vector<handle>& nodes,
            const std::vector<handle>& bones,
            const std::function<void(sm::node&)>& nodes_fn,
            const std::function<void(sm::bone&)>& bones_fn
        );

        static command make_transform_node_positions_command(
            project& proj,
            const std::vector<std::tuple<handle, sm::point>>& old_locs,
            const std::vector<std::tuple<handle, sm::point>>& new_locs
        );

        static command make_add_tab_command(const std::string& tab_name);
    };
}
