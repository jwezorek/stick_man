#pragma once

#include <vector>
#include <string>
#include <expected>
#include <variant>
#include "../core/sm_types.h" 
#include "project.h"

namespace ui {

    class commands {
        friend class project;
    private:

        struct skel_piece_handle {
            std::string skel_name;
            std::string piece_name;
        };

        struct create_node_state {
            std::string tab_name;
            std::string skeleton;
            sm::point loc;
        };

        struct add_bone_state {
            std::string canvas_name;
            skel_piece_handle u_hnd;
            skel_piece_handle v_hnd;
            sm::world original;
            std::string merged;

            add_bone_state(const std::string& cn, sm::node& u, sm::node& v);
        };

        struct rename_state {
            skel_piece_handle old_handle;
            skel_piece_handle new_handle;
        };

        struct replace_skeleton_state {
            std::string canvas_name;
            std::vector<std::string> replacee_names;
            sm::world replacees;
            sm::world replacements; 
            std::vector<std::string> replacement_names;

            replace_skeleton_state(const std::string& canv,
                const std::vector<std::string>& replacees,
                const std::vector<sm::skel_ref>& replacements);
        };

        template<sm::is_skel_piece T>
        static void rename(project& proj, skel_piece_handle old_hnd, skel_piece_handle new_hnd) {
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
            ui::project& proj, const skel_piece_handle& hnd) {
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

        static sm::expected_skel skel_from_handle(ui::project& proj, const skel_piece_handle& hnd);

        template<sm::is_skel_piece T>
        static T& from_handle(ui::project& proj, const skel_piece_handle& hnd) {
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
                skel_piece_handle{ new_name, {} } :
                skel_piece_handle{ old_handle.skel_name, new_name };
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

        static skel_piece_handle to_handle(const ui::skel_piece& piece);
        static ui::command make_create_node_command(const std::string& tab, const sm::point& pt);
        static ui::command make_add_bone_command(const std::string& tab, sm::node& u, sm::node& v);
        static ui::command make_replace_skeletons_command(
            const std::string& canvas_name,
            const std::vector<std::string>& replacees,
            const std::vector<sm::skel_ref>& replacements
        );
    };
}