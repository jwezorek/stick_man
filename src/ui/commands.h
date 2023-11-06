#pragma once

#include <vector>
#include <string>
#include <expected>
#include <variant>
#include "../core/sm_types.h" 
#include "project.h"
#include <unordered_map>

/*------------------------------------------------------------------------------------------------*/

namespace ui {

    class commands {
        friend class project;
    private:

        template<typename T>
        using handle_table = std::unordered_map< handle, T, handle_hash>;


        struct create_node_state {
            std::string tab_name;
            std::string skeleton;
            sm::point loc;
        };

        struct add_bone_state {
            std::string canvas_name;
            handle u_hnd;
            handle v_hnd;
            sm::world original;
            std::string merged;

            add_bone_state(const std::string& str, 
                const handle& u_hnd,
                const handle& v_hnd);
        };

        struct rename_state {
            handle old_handle;
            handle new_handle;
        };

        struct replace_skeleton_state {
            std::string canvas_name;
            std::vector<std::string> replacee_names;
            sm::world replacees;
            std::vector<std::string> replacement_names;
            sm::world replacements;

            replace_skeleton_state(const std::string& canv,
                const std::vector<std::string>& replacees,
                const std::vector<sm::skel_ref>& replacements);
        };

        struct trasform_nodes_and_bones_state {
            std::string canvas;
            std::function<void(sm::node&)> transform_nodes;
            std::function<void(sm::bone&)> transform_bones;
            std::vector<handle> nodes;
            std::vector<handle> bones;
            handle_table<sm::point> old_node_to_position;
            handle_table<sm::rot_constraint> old_bone_to_rotcon;

            trasform_nodes_and_bones_state(
                const std::string& canvas_name,
                const std::vector<sm::node_ref>& nodes,
                const std::function<void(sm::node&)>& fn
            );

            trasform_nodes_and_bones_state(
                const std::string& canvas_name,
                const std::vector<sm::bone_ref>& bones,
                const std::function<void(sm::bone&)>& fn
            );
        };

        template<sm::is_skel_piece T>
        static void rename(project& proj, handle old_hnd, handle new_hnd) {
            auto& old_obj = from_handle<T>(proj, old_hnd);
            if (!new_hnd.piece_name.empty()) {
                proj.rename_aux(std::ref(old_obj), new_hnd.piece_name);
            }
            else {
                proj.rename_aux(std::ref(old_obj), new_hnd.skel_name);
            }
        }

        template<sm::is_node_or_bone T>
        static std::expected<std::reference_wrapper<T>, sm::result> from_handle_aux(
            ui::project& proj, const handle& hnd) {
            auto skel = proj.world_.skeleton(hnd.skel_name);
            if (!skel) {
                return std::unexpected(skel.error());
            }

            auto maybe_piece = skel->get().get_by_name<T>(hnd.piece_name);
            if (!maybe_piece) {
                return std::unexpected(sm::result::not_found);
            }
            return *maybe_piece;
        }

        static sm::expected_skel skel_from_handle(ui::project& proj, const handle& hnd);

        template<sm::is_skel_piece T>
        static T& from_handle(ui::project& proj, const handle& hnd) {
            if constexpr (std::is_same<T, sm::skeleton>::value) {
                auto val = skel_from_handle(proj, hnd);
                if (!val) {
                    throw std::runtime_error("invalid handle to skeleton");
                }
                return val->get();
            }
            else {
                auto val = from_handle_aux<T>(proj, hnd);
                if (!val) {
                    throw std::runtime_error("invalid handle to node/bone");
                }
                return val->get();
            }
        }

        template<sm::is_skel_piece T>
        static ui::command make_rename_command(skel_piece piece, const std::string& new_name) {
            auto old_handle = to_handle(piece);
            auto new_handle = (old_handle.piece_name.empty()) ?
                handle{ new_name, {} } :
                handle{ old_handle.skel_name, new_name };
            auto state = std::make_shared<rename_state>(old_handle, new_handle);

            return {
                [state](ui::project& proj) {
                    rename<T>(proj, state->old_handle, state->new_handle);
                },
                [state](ui::project& proj) {
                    rename<T>(proj, state->new_handle, state->old_handle);
                }
            };
        }

        static ui::command make_create_node_command(const std::string& tab, const sm::point& pt);
        static ui::command make_add_bone_command(const std::string& tab,
            const handle& u_hnd, const handle& v_hnd);
        static ui::command make_replace_skeletons_command(
            const std::string& canvas_name,
            const std::vector<std::string>& replacees,
            const std::vector<sm::skel_ref>& replacements
        );
        static ui::command make_transform_bones_or_nodes_command(
            const std::string& canvas_name,
            const std::vector<sm::node_ref>& nodes,
            const std::vector<sm::bone_ref>& bones,
            const std::function<void(sm::node&)>& nodes_fn,
            const std::function<void(sm::bone&)>& bones_fn
        );
    };
}