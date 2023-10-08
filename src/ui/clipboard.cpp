#include "clipboard.h"
#include "../core/sm_skeleton.h"
#include "../core/json.hpp"
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
#include <limits>
#include <sstream>
/*------------------------------------------------------------------------------------------------*/

namespace r = std::ranges;
namespace rv = std::ranges::views;
using json = nlohmann::json;

namespace {

    template<class... Ts> struct overload : Ts... { using Ts::operator()...; };

    class skeleton_piece_set {
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

    std::string get_prefix(const std::string& name) {
        if (!name.contains('-')) {
            return name;
        }
        int n = static_cast<int>(name.size());
        int i;
        bool at_least_one_digit = false;
        for (i = n - 1; i >= 0; --i) {
            if (!std::isdigit(name[i])) {
                break;
            } else {
                at_least_one_digit = true;
            }
        }
        if (!at_least_one_digit || i < 0) {
            return name;
        }
        if (name[i] != '-') {
            return name;
        }
        return name.substr(0, i);
    }

    std::string unique_skeleton_name(const std::string& old_name,
            const std::vector<std::string>& used_names) {
        auto is_not_unique = r::find_if( used_names, 
                [old_name](auto&& str) {return str == old_name; }
            ) != used_names.end();
        return (is_not_unique) ?
            ui::make_unique_name(used_names, get_prefix(old_name)) :
            old_name;
    }

    sm::skeleton* create_skeleton(sm::world& dest, const std::string& skel_name) {
        std::string name = skel_name;
        if (dest.contains_skeleton_name(name)) {
            name = unique_skeleton_name(name, dest.skeleton_names());
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
                    bool is_root = dest_skel->empty();
                    auto copy = node.copy_to(*dest_skel);
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

    // returns the pieces of the skeletons in a given set such that pieces are topologically 
    // ordered per skeleton. Internal pieces of selected whole skeletons are not returned, 
    // just the an item for the whole skeleton.

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

    // given a set of skeletons generate separate skeletons for each connected component
    // of selected-ness or deselected-ness of the skeletons' nodes and bones. Since you
    // cannot have a bone without its two nodes existing this will make duplicate
    // nodes for connected components trees with raw bones for leaves, but this is what 
    // we want. This is what representing arbitrary selections as skeletons entails. 

    std::tuple<sm::world, sm::world> split_skeletons_by_selection( 
            ui::canvas& canv, const std::unordered_set<sm::skeleton*>& skel_set) {
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
                    [&](sm::skel_ref skel) {
                        copied.insert(skel);
                        auto new_skel = skel.get().copy_to(
                            dest_world,
                            unique_skeleton_name(skel.get().name(), dest_world.skeleton_names())
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

    // operations that involve doing something with the current selection
    enum class selection_operation {
        cut, copy, del
    };

    json perform_op_on_selection(ui::stick_man& main_wnd, selection_operation op) {
        auto& world = main_wnd.sandbox();
        auto& canv = main_wnd.canvases().active_canvas();
        auto relavent_skels = relavent_skeleton_set(canv);

        auto [unselected, selected] = split_skeletons_by_selection(canv, relavent_skels);

        if (op == selection_operation::cut || op == selection_operation::del) {
            canv.clear();
            for (sm::skeleton* skel : relavent_skels) {
                world.delete_skeleton(skel->name());
            }

            for (auto skel : unselected.skeletons()) {
                auto copy = skel.get().copy_to(
                    world,
                    unique_skeleton_name(skel.get().name(), world.skeleton_names())
                );
                copy->get().insert_tag("tab:" + canv.tab_name());
            }
            main_wnd.canvases().sync_to_model(main_wnd.sandbox(), canv);
        }

        if (op == selection_operation::cut || op == selection_operation::copy) {
            for (auto skel : selected.skeletons()) {
                skel.get().clear_tags();
            }
            return selected.to_json();
        }

        return {};
    }

    QByteArray cut_selection(ui::stick_man& main_wnd) {
        auto selection_json = perform_op_on_selection(main_wnd, selection_operation::cut);
        auto str = selection_json.dump(4);
        return QByteArray(str.c_str(), str.size());
    }

    QByteArray copy_selection(ui::stick_man& main_wnd) {
        auto selection_json = perform_op_on_selection(main_wnd, selection_operation::copy);
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
            rv::transform([](auto s) {return s.get().root_node().get().world_pos(); });
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
        clipboard_world.from_json(world_json_str);

        auto& canvases = main_wnd.canvases();
        auto& canv = canvases.active_canvas();
        auto& dest_world = main_wnd.sandbox();

        auto dest_mat = (!in_place) ? 
            paste_matrix(canv.cursor_pos(), clipboard_world) :
            std::optional<sm::matrix>{};
        if (dest_mat) {
            clipboard_world.apply( *dest_mat );
        }

        for (auto skel : clipboard_world.skeletons()) {
            auto copy = skel.get().copy_to(
                dest_world,
                unique_skeleton_name(skel.get().name(), dest_world.skeleton_names())
            );
            copy->get().insert_tag("tab:" + canv.tab_name());
        }
        


        canvases.sync_to_model(dest_world, canv);
    }

    void cut_or_copy(ui::stick_man& main_wnd, bool should_cut) {
        auto bytes = should_cut ? cut_selection(main_wnd) : copy_selection(main_wnd);
        QClipboard* clipboard = QApplication::clipboard();

        QMimeData* mime_data = new QMimeData;
        mime_data->setData("application/x-stick_man", bytes);

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
    if (mimeData->hasFormat("application/x-stick_man")) {
        QByteArray bytes = mimeData->data("application/x-stick_man");
        paste_selection(main_wnd, bytes, in_place);
    }
}

void ui::clipboard::del(stick_man& main_wnd) {
    perform_op_on_selection(main_wnd, selection_operation::del);
}