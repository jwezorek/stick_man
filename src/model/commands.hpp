#pragma once
#include <vector>
#include <string>
#include <expected>
#include <variant>
#include "../core/sm_types.hpp"
#include "../core/sm_visit.hpp"
#include "project.hpp"
#include <unordered_map>
#include <unordered_set>
#include <optional>
#include <stdexcept>

/*------------------------------------------------------------------------------------------------*/

namespace mdl {

    class commands {
        friend class project;
    private:
        template<typename T>
        using handle_table = std::unordered_map<handle, T>;

        template<sm::is_skel_piece T>
        static T& resolve(project& proj, const handle& hnd) {
            auto object = proj.get(hnd);
            if (auto ref = std::get_if<sm::ref<T>>(&object)) {
                return ref->get();
            }
            throw std::runtime_error("model object has unexpected type");
        }
        struct create_node_state {
            std::string node_name;
            sm::object_id skeleton;
            sm::point loc;
            sm::topology snapshot;
        };
        struct add_bone_state {
            std::string bone_name;
            handle u_hnd;
            handle v_hnd;
            sm::topology original;
            sm::membership_state membership;
            sm::constraint_map before_constraints;
            std::optional<sm::constraint_map> after_constraints;
            sm::result status = sm::result::success;
            sm::object_id merged;
            std::optional<sm::object_id> bone_id;
            std::vector<sm::object_id> cascade_characters;
            add_bone_state(const std::string& bone_name,
                const handle& u_hnd,
                const handle& v_hnd,
                const sm::topology_edit_effects& effects);
        };
        struct rename_state {
            handle object;
            std::string old_name;
            std::string new_name;
        };
        struct replace_skeleton_state {
            std::vector<sm::object_id> replacee_ids;
            sm::topology replacees;
            std::vector<sm::object_id> replacement_ids;
            sm::topology replacements;
            sm::membership_state before_membership;
            std::optional<sm::membership_state> after_membership;
            sm::constraint_map before_constraints;
            std::optional<sm::constraint_map> after_constraints;
            sm::result status = sm::result::success;
            std::unordered_set<sm::object_id> regenerate_ids;
            std::vector<sm::object_id> cascade_characters;
            replace_skeleton_state(
                const std::vector<sm::object_id>& replacees,
                const std::vector<sm::skel_ref>& replacements,
                const std::unordered_set<sm::object_id>& regenerate_ids,
                const sm::topology_edit_effects& effects);
        };
        struct transform_nodes_and_bones_state {
            std::function<void(sm::node&)> transform_nodes;
            std::function<void(sm::bone&)> transform_bones;
            std::vector<handle> nodes;
            std::vector<handle> bones;
            handle_table<sm::point> old_node_to_position;
            sm::constraint_map old_constraints;
            std::optional<sm::constraint_map> new_constraints;
            handle_table<sm::point> new_node_to_position;
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
        static command make_rename_command(sm::ref<T> piece, const std::string& new_name) {
            auto state = std::make_shared<rename_state>(
                to_handle(skel_piece{piece}), piece->name(), new_name);
            return {
                [state](mdl::project& proj) {
                    proj.rename_aux(state->object, state->new_name);
                },
                [state](mdl::project& proj) {
                    proj.rename_aux(state->object, state->old_name);
                }
            };
        }
        static command make_create_node_command(
            const sm::point& pt, const std::string& node_name);
        static command make_add_bone_command(
            const handle& u_hnd, const handle& v_hnd, const std::string& bone_name,
            const sm::topology_edit_effects& effects);
        static command make_replace_skeletons_command(
            const std::vector<sm::object_id>& replacees,
            const std::vector<sm::skel_ref>& replacements,
            const std::unordered_set<sm::object_id>& regenerate_ids,
            const sm::topology_edit_effects& effects
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
    };
}
