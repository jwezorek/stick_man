#include "clipboard.h"
#include "../core/sm_skeleton.h"
#include "../core/json.hpp"
#include "canvas/node_item.h"
#include "canvas/bone_item.h"
#include "canvas/skel_item.h"
#include "canvas/scene.h"
#include "canvas/canvas_manager.h"
#include "../model/project.h"
#include "stick_man.h"
#include "util.h"
#include <tuple>
#include <vector>
#include <unordered_map>
#include <variant>
#include <ranges>
#include <memory>
#include <limits>
#include <sstream>

/*------------------------------------------------------------------------------------------------*/

namespace r = std::ranges;
namespace rv = std::ranges::views;
using json = nlohmann::json;

namespace {

    template<class... Ts> struct overload : Ts... { using Ts::operator()...; };

    static const QByteArray k_stickman_mime_type = "application/x-stick_man";

    class skeleton_piece_set {
        std::unordered_set<const void*> impl_;

        template<typename T>
        static const void* to_void_star(const T& v) {
            return reinterpret_cast<const void*>(&v);
        }

    public:
        skeleton_piece_set() {};

        bool contains(mdl::const_skel_piece sp) const {
            return std::visit(
                [this](auto itm_ref)->bool {
                    return impl_.contains(to_void_star(itm_ref.get()));
                },
                sp
            );
        }

        void insert(mdl::const_skel_piece piece) {
            std::visit(
                [&](auto itm) {
                    impl_.insert(
                        to_void_star(itm.get())
                    );
                },
                piece
            );
        }

        void insert_range(auto rng) {
            for (mdl::const_skel_piece piece : rng) {
                insert(piece);
            }
        }
    };

    sm::skeleton* create_skeleton(sm::world& dest, const std::string& skel_name) {
        std::string name = skel_name;
        if (dest.contains_skeleton(name)) {
            name = mdl::unique_skeleton_name(name, dest.skeleton_names());
        }
        auto skel = dest.create_skeleton(name);
        return &(skel->get());
    }

    void copy_connected_component(sm::world& dest, const auto& root,
            const skeleton_piece_set& selection, skeleton_piece_set& copied) {

        bool is_selected = selection.contains(std::ref(root));
        auto is_part_of_component = [&](auto& itm)->bool {
                return is_selected == selection.contains(itm);
            };

        sm::skeleton* dest_skel = nullptr;
        auto node_visitor = [&](const sm::node& node)->sm::visit_result {
                if (!is_part_of_component(node)) {
                    return sm::visit_result::terminate_branch;
                }
                copied.insert(node);
                
                if (!dest_skel) {
                    dest_skel = create_skeleton(dest, node.owner().name());
                }
                if (!dest_skel->contains<sm::node>(node.name())) {
                    bool is_root = dest_skel->empty();
                    auto copy = node.copy_to(*dest_skel);
                }

                return sm::visit_result::continue_traversal;
            };

        auto bone_visitor = [&](const sm::bone& bone)->sm::visit_result {
                if (!is_part_of_component(bone)) {
                    return sm::visit_result::terminate_branch;
                }
                copied.insert(bone);

                if (!dest_skel) {
                    dest_skel = create_skeleton(dest, bone.owner().name());
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

    std::unordered_set<const sm::skeleton*> get_selected_skeletons(ui::canvas::scene& canv) {
        std::unordered_set<const sm::skeleton*> selected_skels;
        auto selected_skel = canv.selected_skeleton();
        if (selected_skel) {
            selected_skels.insert(&selected_skel->model());
        } else {
            for (auto skel_item : canv.skeleton_items()) {
                auto& skel = skel_item->model();
                bool skel_is_selected = r::all_of(skel.nodes(),
                        [](sm::node_ref nr)->bool {
                            auto& ni = ui::canvas::item_from_model<ui::canvas::node_item>(nr.get());
                            return ni.is_selected();
                        }
                    ) && r::all_of(skel.bones(),
                        [](sm::bone_ref br)->bool {
                            auto& bi = ui::canvas::item_from_model<ui::canvas::bone_item>(br.get());
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

    // returns the pieces of the skeletons in a given set such that pieces are topologically 
    // ordered per skeleton. Internal pieces of selected whole skeletons are not returned, 
    // just the an item for the whole skeleton.

    std::vector<std::tuple<mdl::const_skel_piece, bool>> skeleton_pieces_in_topological_order(
            ui::canvas::scene& canv, const std::unordered_set<const sm::skeleton*>& skel_set) {

        auto selected_skeletons = get_selected_skeletons(canv);
        auto pieces_and_sel_state = selected_skeletons |
            rv::transform(
                [](const sm::skeleton* p)->std::tuple<mdl::const_skel_piece, bool> {
                    return { std::ref(*p), true };
                }
            ) | r::to<std::vector<std::tuple<mdl::const_skel_piece, bool>>>();

        for (auto skel_ptr : skel_set) {
            if (selected_skeletons.contains(skel_ptr)) {
                continue;
            }

            sm::dfs(
                skel_ptr->root_node(),
                [&](const sm::node& node)->sm::visit_result {
                    auto& ni = ui::canvas::item_from_model<ui::canvas::node_item>(node);
                    pieces_and_sel_state.emplace_back(
                        std::ref(node), ni.is_selected()
                    );
                    return sm::visit_result::continue_traversal;
                },
                [&](const sm::bone& bone)->sm::visit_result {
                    auto& bi = ui::canvas::item_from_model<ui::canvas::bone_item>(bone);
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

    // given a set of skeletons generate separate skeletons for each connected component
    // of selected-ness or deselected-ness of the skeletons' nodes and bones. Since you
    // cannot have a bone without its two nodes existing this will make duplicate
    // nodes for connected components trees with raw bones for leaves, but this is what 
    // we want. This is what representing arbitrary selections as skeletons entails. 

    std::tuple<sm::world, sm::world> split_skeletons_by_selection( 
            ui::canvas::scene& canv, const std::unordered_set<const sm::skeleton*>& skel_set) {
        auto pieces = skeleton_pieces_in_topological_order(canv, skel_set);

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
                    [&](sm::const_skel_ref skel) {
                        copied.insert(skel);
                        auto new_skel = skel.get().copy_to(
                            dest_world,
                            mdl::unique_skeleton_name(
                                skel.get().name(), dest_world.skeleton_names()
                            )
                        );
                        if (!new_skel) {
                            throw std::runtime_error("unable to make new skeleton");
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

    // returns the the set of skeletons that are either selected or contain at least one 
    // node or bone that is selected, 

    std::unordered_set<const sm::skeleton*> relavent_skeleton_set(ui::canvas::scene& canv) {
        return canv.selection() |
            rv::transform(
                [](ui::canvas::canvas_item* itm)->const sm::skeleton* {
                    return std::visit(
                        overload{
                            [](sm::skel_ref skel)->const sm::skeleton* {
                                return &skel.get();
                            },
                            [](auto node_or_bone)->const sm::skeleton* {
                                auto& skel = node_or_bone.get().owner();
                                return &skel;
                            }
                        },
                        itm->to_skeleton_piece()
                    );
                }
            ) | r::to<std::unordered_set<const sm::skeleton*>>();
    }

    // operations that involve doing something with the current selection
    enum class selection_operation {
        cut, copy, del
    };

    json perform_op_on_selection(ui::stick_man& main_wnd, selection_operation op) {
        auto& project = main_wnd.project();
        auto& canv = main_wnd.canvases().active_canvas();
        auto relavent_skels = relavent_skeleton_set(canv);

        auto [unselected, selected] = split_skeletons_by_selection(canv, relavent_skels);

        if (op == selection_operation::cut || op == selection_operation::del) {
            auto replacees = relavent_skels | rv::transform(
                    [](const auto* skel) {
                        return skel->name();
                    }
                ) | r::to<std::vector<std::string>>();
            auto replacements = unselected.skeletons() | r::to<std::vector<sm::skel_ref>>();
            project.replace_skeletons(canv.tab_name(), replacees, replacements);
        }

        if (op == selection_operation::cut || op == selection_operation::copy) {
            return selected.to_json();
        }

        return {};
    }

    QByteArray cut_or_copy_selection(ui::stick_man& main_wnd, selection_operation op) {
        auto selection_json = perform_op_on_selection(main_wnd, op);
        auto str = selection_json.dump(4);
        return QByteArray(str.c_str(), str.size());
    }

    std::optional<sm::matrix> paste_matrix(std::optional<sm::point> target,const sm::world& world) {
        if (!target) {
            return {};
        }
        sm::point lower_left = {
            std::numeric_limits<double>::max(),
            std::numeric_limits<double>::max()
        };

        auto pts = world.skeletons() |
            rv::transform([](auto s) {return s.get().root_node().world_pos(); });
        for (auto pt : pts) {
            if (pt.y < lower_left.y || (pt.y == lower_left.y && pt.x < lower_left.x)) {
                lower_left = pt;
            }
        }
        return sm::translation_matrix(*target - lower_left);
    }

    void paste_selection(ui::stick_man& main_wnd, const QByteArray& bytes, bool in_place) {
        std::string world_json_str = std::string(bytes.data());
        sm::world clipboard_world;
        clipboard_world.from_json_str(world_json_str);

        auto& canvases = main_wnd.canvases();
        auto& canv = canvases.active_canvas();

        auto dest_mat = (!in_place) ? 
            paste_matrix(canv.cursor_pos(), clipboard_world) :
            std::optional<sm::matrix>{};
        if (dest_mat) {
            clipboard_world.apply( *dest_mat );
        }

        auto& project = main_wnd.project();
        project.replace_skeletons(
            canv.tab_name(),
            {},
            clipboard_world.skeletons() | r::to<std::vector<sm::skel_ref>>()
        );
    }

    void cut_or_copy(ui::stick_man& main_wnd, bool should_cut) {
        QClipboard* clipboard = QApplication::clipboard();

        QMimeData* mime_data = new QMimeData;
        mime_data->setData(
            k_stickman_mime_type, 
            cut_or_copy_selection(
                main_wnd,
                should_cut ? selection_operation::cut : selection_operation::copy
            )
        );

        clipboard->setMimeData(mime_data);
    }
}
 
void ui::clipboard::cut(stick_man& main_wnd) {
    cut_or_copy(main_wnd, true);
}

void ui::clipboard::copy(stick_man& main_wnd) {
    cut_or_copy(main_wnd, false);
}

void ui::clipboard::paste(stick_man& main_wnd, bool in_place) {
    QClipboard* clipboard = QApplication::clipboard();
    const QMimeData* mimeData = clipboard->mimeData();
    if (mimeData->hasFormat(k_stickman_mime_type)) {
        QByteArray bytes = mimeData->data(k_stickman_mime_type);
        paste_selection(main_wnd, bytes, in_place);
    }
}

void ui::clipboard::del(stick_man& main_wnd) {
    perform_op_on_selection(main_wnd, selection_operation::del);
}