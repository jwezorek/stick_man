#include "clipboard.h"
#include "../core/sm_skeleton.h"
#include "canvas_item.h"
#include "canvas.h"
#include "stick_man.h"
#include "util.h"
#include <tuple>
#include <vector>
#include <unordered_map>
#include <variant>
#include <ranges>
#include <memory>

/*------------------------------------------------------------------------------------------------*/

namespace r = std::ranges;
namespace rv = std::ranges::views;

namespace {

    template<class... Ts> struct overload : Ts... { using Ts::operator()...; };

    class skeleton_piece_set {
        //TODO: make this const correct...
        std::unordered_map<void*, ui::skeleton_piece> impl_;

        template<typename T>
        static void* to_void_star(const T& v) {
            return reinterpret_cast<void*>(
                const_cast<T*>(&v)
            );
        }

    public:
        skeleton_piece_set() {};

        bool contains(auto& p) const {
            return contains(
                ui::skeleton_piece{ std::ref(p) }
            );
        }

        bool contains(ui::skeleton_piece sp) const {
            return std::visit(
                [this](auto itm_ref)->bool {
                    return impl_.contains(to_void_star(itm_ref.get()));
                },
                sp
            );
        }

        void insert(ui::skeleton_piece piece) {
            std::visit(
                [&](auto itm) {
                    impl_.insert(
                        {to_void_star(itm.get()), piece }
                    );
                },
                piece
            );
        }

        void insert_range(auto rng) {
            for (ui::skeleton_piece piece : rng) {
                insert(piece);
            }
        }

        auto items() const {
            return impl_ | rv::transform(
                [](const auto& pair)->ui::skeleton_piece {
                    return pair.second;
                }
            );
        }
    };

    sm::skeleton* create_skeleton(sm::world& dest, const std::string& skel_name) {
        std::string name = skel_name;
        if (dest.contains_skeleton_name(name)) {
            name = ui::make_unique_name(dest.skeleton_names(), "skeleton");
        }
        auto skel = dest.create_skeleton(name);
        return &(skel->get());
    }

    void copy_connected_component(sm::world& dest, auto& root,
            const skeleton_piece_set& selection, skeleton_piece_set& copied) {

        bool is_selected = selection.contains(std::ref(root));
        auto is_part_of_component = [&](auto& itm)->bool {
                return is_selected == selection.contains(itm);
            };

        sm::skeleton* dest_skel = nullptr;
        auto node_visitor = [&](sm::node& node)->sm::visit_result {
                if (!is_part_of_component(node)) {
                    return sm::visit_result::terminate_branch;
                }
                copied.insert(node);
                
                if (!dest_skel) {
                    dest_skel = create_skeleton(dest, node.owner().get().name());
                }
                if (!dest_skel->contains<sm::node>(node.name())) {
                    node.copy_to(*dest_skel);
                }

                return sm::visit_result::continue_traversal;
            };

        auto bone_visitor = [&](sm::bone& bone)->sm::visit_result {
                if (!is_part_of_component(bone)) {
                    return sm::visit_result::terminate_branch;
                }
                copied.insert(bone);

                if (!dest_skel) {
                    dest_skel = create_skeleton(dest, bone.owner().get().name());
                }
                if (!dest_skel->get_by_name<sm::node>(bone.parent_node().name())) {
                    bone.parent_node().copy_to( *dest_skel );
                }
                bone.child_node().copy_to( *dest_skel );
                bone.copy_to( *dest_skel );

                return sm::visit_result::continue_traversal;
            };

        sm::dfs(
            root,
            node_visitor,
            bone_visitor,
            true
        );
    }

    // this functions returns the sm::skeleton associated with the one-and-only-one selected
    // skeleton item in the UI *or* it returns any sm::skeletons for which all of their nodes
    // and bones are selected.

    std::unordered_set<sm::skeleton*> get_selected_skeletons(ui::canvas& canv) {
        std::unordered_set<sm::skeleton*> selected_skels;
        auto selected_skel = canv.selected_skeleton();
        if (selected_skel) {
            selected_skels.insert(&selected_skel->model());
        } else {
            for (auto skel_item : canv.skeleton_items()) {
                auto& skel = skel_item->model();
                bool skel_is_selected = r::all_of(skel.nodes(),
                        [](sm::node_ref nr)->bool {
                            auto& ni = ui::item_from_model<ui::node_item>(nr.get());
                            return ni.is_selected();
                        }
                    ) && r::all_of(skel.bones(),
                        [](sm::bone_ref br)->bool {
                            auto& bi = ui::item_from_model<ui::bone_item>(br.get());
                            return bi.is_selected();
                        }
                    );
                if (skel_is_selected) {
                    selected_skels.insert(&skel);
                }
            }
        }
        return selected_skels;
    }

    std::vector<std::tuple<ui::skeleton_piece, bool>> skeleton_pieces_in_topological_order(
            ui::canvas& canv, const std::unordered_set<sm::skeleton*>& relavent_skel_set) {

        auto selected_skeletons = get_selected_skeletons(canv);
        auto pieces_and_sel_state = selected_skeletons |
            rv::transform(
                [](sm::skeleton* p)->std::tuple<ui::skeleton_piece, bool> {
                    return { std::ref(*p), true };
                }
            ) | r::to<std::vector<std::tuple<ui::skeleton_piece, bool>>>();

        for (auto skel_item : canv.skeleton_items()) {
            auto& skel = skel_item->model();

            if (selected_skeletons.contains(&skel)) {
                continue;
            }

            if (!relavent_skel_set.contains(&skel)) {
                continue;
            }

            sm::dfs(
                skel.root_node(),
                [&](sm::node& node)->sm::visit_result {
                    auto& ni = ui::item_from_model<ui::node_item>(node);
                    pieces_and_sel_state.emplace_back(
                        std::ref(node), ni.is_selected()
                    );
                    return sm::visit_result::continue_traversal;
                },
                [&](sm::bone& bone)->sm::visit_result {
                    auto& bi = ui::item_from_model<ui::bone_item>(bone);
                    pieces_and_sel_state.emplace_back(
                        std::ref(bone), bi.is_selected()
                    );
                    return sm::visit_result::continue_traversal;
                },
                true
            );
        }

        return pieces_and_sel_state;
    }

    std::tuple<sm::world, sm::world> split_skeletons_by_selection( 
            ui::canvas& canv, const std::unordered_set<sm::skeleton*>& relavent_skel_set) {
        auto pieces = skeleton_pieces_in_topological_order(canv, relavent_skel_set);

        skeleton_piece_set selection_set;
        for (auto [piece, is_selected] : pieces) {
            if (is_selected) {
                selection_set.insert(piece);
            }
        }

        skeleton_piece_set copied;
        sm::world unselected;
        sm::world selected;

        for (auto [piece, is_selected] : pieces) {
            if (copied.contains(piece)) {
                continue;
            }
            auto& dest_world = is_selected ? selected : unselected;
            std::visit(
                overload{
                    [&](sm::skel_ref skel) {
                        copied.insert(skel);
                        auto new_skel = skel.get().copy_to(dest_world);
                        if (!new_skel) {
                            new_skel = skel.get().copy_to(dest_world,
                                ui::make_unique_name(dest_world.skeleton_names(), "skeleton")
                            );
                            if (!new_skel) {
                                throw std::runtime_error("unable to make new skeleton");
                            }
                        }
                    },
                    [&](auto node_or_bone) {
                        copy_connected_component(dest_world, node_or_bone, selection_set, copied);
                    }
                },
                piece
            );
        }
        return { std::move(unselected), std::move(selected) };
    }

    std::unordered_set<sm::skeleton*> relavent_skeleton_set(ui::canvas& canv) {
        return canv.selection() |
            rv::transform(
                [](ui::abstract_canvas_item* itm)->sm::skeleton* {
                    return std::visit(
                        overload{
                            [](sm::skel_ref skel)->sm::skeleton* {
                                return &skel.get();
                            },
                            [](auto node_or_bone)->sm::skeleton* {
                                auto skel = node_or_bone.get().owner();
                                return &skel.get();
                            }
                        },
                        itm->to_skeleton_piece()
                    );
                }
            ) | r::to<std::unordered_set<sm::skeleton*>>();
    }
}

QByteArray ui::cut_selection(stick_man& canv)
{
    return QByteArray();
}

QByteArray ui::copy_selection(stick_man& canv)
{
    return QByteArray();
}

void ui::delete_selection(stick_man& main_wnd) {
    auto& world = main_wnd.sandbox();
    auto& canv = main_wnd.canvases().active_canvas();
    auto relavent_skels = relavent_skeleton_set( canv );

    auto [unselected, selected] = split_skeletons_by_selection(canv, relavent_skels);
    canv.clear();
    for (sm::skeleton* skel : relavent_skels) {
        world.delete_skeleton(skel->name());
    }
    
    for (auto skel : unselected.skeletons()) {
        auto copy = skel.get().copy_to(world);
        if (!copy) {
            auto new_name = make_unique_name(
                main_wnd.sandbox().skeleton_names(), "skeleton"
            );
            copy = skel.get().copy_to(world, new_name);
        }
        copy->get().insert_tag("tab:" + canv.tab_name());
    }
    main_wnd.canvases().sync_to_model(main_wnd.sandbox(), canv);
}

void ui::paste_selection(stick_man& canv, const QByteArray& bytes)
{
}

void ui::debug(canvas& canv)
{
    /*
    std::vector<skeleton_item*> skel_items = canv.skeleton_items();
    auto& skel = skel_items.front()->model();
    auto sel_set = bone_and_node_set_from_canvas(canv);
    sm::world unselected;
    sm::world selected;
    split_skeleton_by_selection(skel, sel_set, unselected, selected);
    auto str = unselected.to_json() + "\n\n" + selected.to_json();
    to_text_file("C:\\test\\clippy.txt", str);
    */
}
