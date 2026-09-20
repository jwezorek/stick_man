#include "clipboard.hpp"
#include "../core/sm_skeleton.hpp"
#include "../core/sm_visit.hpp"
#include "../core/third-party/json.hpp"
#include "canvas/node_item.hpp"
#include "canvas/bone_item.hpp"
#include "canvas/skel_item.hpp"
#include "canvas/scene.hpp"
#include "canvas/canvas_manager.hpp"
#include "../model/project.hpp"
#include "stick_man.hpp"
#include "util.hpp"
#include <tuple>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <optional>
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
    sm::skeleton* create_skeleton(sm::topology& dest, const std::string& skel_name) {
        auto skel = dest.create_skeleton(skel_name);
        return skel ? &skel->get() : nullptr;
    }

    void copy_connected_component(sm::topology& dest, const auto& root,
            const skeleton_piece_set& selection, skeleton_piece_set& copied) {
        bool is_selected = selection.contains(sm::ref(root));
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
                if (!dest_skel->contains<sm::node>(node.id())) {
                    bool is_root = dest_skel->empty();
                    auto copy = node.copy_to(dest, dest_skel->id());
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
                if (!dest_skel->get<sm::node>(bone.parent_node().id())) {
                    bone.parent_node().copy_to(dest, dest_skel->id());
                }
                bone.child_node().copy_to(dest, dest_skel->id());
                bone.copy_to(dest, dest_skel->id());

                return sm::visit_result::continue_traversal;
            };
        sm::visit_nodes_and_bones( root, node_visitor, bone_visitor, true );
    }

    // Include explicit skeleton selections and skeletons whose nodes and bones are all selected.
    std::unordered_set<const sm::skeleton*> get_selected_skeletons(ui::canvas::scene& canv) {
        std::unordered_set<const sm::skeleton*> selected_skels;
        for (auto* selected_skel : canv.selected_skeletons()) {
            selected_skels.insert(&selected_skel->model());
        }
        for (auto skel_item : canv.skeleton_items()) {
            auto& skel = skel_item->model();
            bool skel_is_selected = r::all_of(skel.nodes(),
                    [](sm::node_ref nr)->bool {
                        auto& ni = ui::canvas::item_from_model<ui::canvas::item::node>(nr.get());
                        return ni.is_selected();
                    }
                ) && r::all_of(skel.bones(),
                    [](sm::bone_ref br)->bool {
                        auto& bi = ui::canvas::item_from_model<ui::canvas::item::bone>(br.get());
                        return bi.is_selected();
                    }
                );
            if (skel_is_selected) {
                selected_skels.insert(&skel);
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
                    return { sm::ref(*p), true };
                }
            ) | r::to<std::vector<std::tuple<mdl::const_skel_piece, bool>>>();
        for (auto skel_ptr : skel_set) {
            if (selected_skeletons.contains(skel_ptr)) {
                continue;
            }
            sm::visit_nodes_and_bones(
                skel_ptr->root_node(),
                [&](const sm::node& node)->sm::visit_result {
                    auto& ni = ui::canvas::item_from_model<ui::canvas::item::node>(node);
                    pieces_and_sel_state.emplace_back(
                        sm::ref(node), ni.is_selected()
                    );
                    return sm::visit_result::continue_traversal;
                },
                [&](const sm::bone& bone)->sm::visit_result {
                    auto& bi = ui::canvas::item_from_model<ui::canvas::item::bone>(bone);
                    pieces_and_sel_state.emplace_back(
                        sm::ref(bone), bi.is_selected()
                    );
                    return sm::visit_result::continue_traversal;
                },
                true
            );
        }
        return pieces_and_sel_state;
    }

    std::unordered_set<sm::object_id> selected_node_ids(
            ui::canvas::scene& canv,
            const std::unordered_set<const sm::skeleton*>& skel_set) {
        std::unordered_set<sm::object_id> ids;
        for (const auto& [piece, is_selected] :
                skeleton_pieces_in_topological_order(canv, skel_set)) {
            if (!is_selected) {
                continue;
            }
            if (auto node = std::get_if<sm::const_node_ref>(&piece)) {
                ids.insert(node->get().id());
            }
        }
        return ids;
    }

    // given a set of skeletons generate separate skeletons for each connected component
    // of selected-ness or deselected-ness of the skeletons' nodes and bones. Since you
    // cannot have a bone without its two nodes existing this will make duplicate
    // nodes for connected components trees with raw bones for leaves, but this is what
    // we want. This is what representing arbitrary selections as skeletons entails.
    std::tuple<sm::topology, sm::topology> split_skeletons_by_selection(
            ui::canvas::scene& canv, const std::unordered_set<const sm::skeleton*>& skel_set) {
        auto pieces = skeleton_pieces_in_topological_order(canv, skel_set);

        skeleton_piece_set selection_set;
        for (auto [piece, is_selected] : pieces) {
            if (is_selected) {
                selection_set.insert(piece);
            }
        }
        skeleton_piece_set copied;
        sm::topology unselected;
        sm::topology selected;
        for (auto [piece, is_selected] : pieces) {
            if (copied.contains(piece)) {
                continue;
            }
            auto& dest_topology = is_selected ? selected : unselected;
            std::visit(
                overload{
                    [&](sm::const_skel_ref skel) {
                        copied.insert(skel);
                        auto new_skel = skel->copy_to(dest_topology);
                        if (!new_skel) {
                            throw std::runtime_error("unable to make new skeleton");
                        }
                    },
                    [&](auto node_or_bone) {
                        copy_connected_component(dest_topology, node_or_bone, selection_set, copied);
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
                [](ui::canvas::item::base* itm)->const sm::skeleton* {
                    return std::visit(
                        overload{
                            [](sm::skel_ref skel)->const sm::skeleton* {
                                return &skel.get();
                            },
                            [](auto node_or_bone)->const sm::skeleton* {
                                auto& skel = node_or_bone->owner();
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
        if (canv.selection().empty()) return {};
        if (auto* character = canv.selected_character()) {
            auto id = character->id();
            sm::topology rig;
            for (auto skel : character->model().rig().skeletons()) {
                if (!skel->copy_to(rig)) return {};
            }
            json payload{{"kind", "character"}, {"name", character->model().name()}, {"topology", rig.to_json()}};
            if (op != selection_operation::del) {
                // A Core package preserves artwork without exposing atlas resources to the editor.
                sm::project resources;
                std::vector<sm::const_skel_ref> members;
                for (auto skel : character->model().rig().skeletons()) {
                    auto copied = resources.copy_skeleton(skel.get());
                    if (!copied) return {};
                    members.emplace_back(copied->get());
                }
                auto copied = resources.create_character(members);
                if (!copied) return {};
                if (resources.set_character_root_bone(copied->get().id(), character->model().character_root_bone()) != sm::result::success)
                    return {};
                resources.artwork(copied->get().id()) = character->model().artwork();
                auto encoded = resources.serialize();
                if (!encoded) return {};
                payload["artwork_package"] = QByteArray(reinterpret_cast<const char*>(encoded->data()),
                    qsizetype(encoded->size())).toBase64().toStdString();
            }
            if (op != selection_operation::copy) {
                if (project.delete_character(id) != sm::result::success) return {};
            }
            return payload;
        }
        auto relavent_skels = relavent_skeleton_set(canv);

        auto regenerate_ids = selected_node_ids(canv, relavent_skels);
        auto [unselected, selected] = split_skeletons_by_selection(canv, relavent_skels);
        if (op == selection_operation::cut || op == selection_operation::del) {
            auto replacees = relavent_skels | rv::transform(
                    [](const auto* skel) {
                        return skel->id();
                    }
                ) | r::to<std::vector<sm::object_id>>();
            auto replacements = unselected.skeletons() | r::to<std::vector<sm::skel_ref>>();
            if (project.replace_skeletons(replacees, replacements, regenerate_ids) != sm::result::success) return {};
        }
        if (op == selection_operation::cut || op == selection_operation::copy) {
            return selected.to_json();
        }

        return {};
    }

    QByteArray cut_or_copy_selection(ui::stick_man& main_wnd, selection_operation op) {
        auto selection_json = perform_op_on_selection(main_wnd, op);
        if (selection_json.is_null()) return {};
        auto str = selection_json.dump(4);
        return QByteArray(str.c_str(), str.size());
    }
    std::optional<sm::matrix> paste_matrix(std::optional<sm::point> target,const sm::topology& topology) {
        if (!target) {
            return {};
        }
        sm::point lower_left = {
            std::numeric_limits<double>::max(),
            std::numeric_limits<double>::max()
        };
        auto pts = topology.skeletons() |
            rv::transform([](auto s) {return s->root_node().world_pos(); });
        for (auto pt : pts) {
            if (pt.y < lower_left.y || (pt.y == lower_left.y && pt.x < lower_left.x)) {
                lower_left = pt;
            }
        }
        return sm::translation_matrix(*target - lower_left);
    }
    void paste_selection(ui::stick_man& main_wnd, const QByteArray& bytes, bool in_place) {
        auto payload = json::parse(bytes.constData(), bytes.constData() + bytes.size(), nullptr, false);
        if (payload.is_discarded() || !payload.is_object()) return;
        if (payload.contains("kind") && !payload["kind"].is_string()) return;
        bool character = payload.value("kind", std::string{}) == "character";
        if (character && (!payload.contains("topology") || !payload.contains("name") || !payload["name"].is_string())) return;
        std::string topology_json_str = (character ? payload["topology"] : payload).dump();
        sm::topology clipboard_topology;
        if (clipboard_topology.from_json_str(topology_json_str) != sm::result::success) return;

        auto& canvases = main_wnd.canvases();
        auto& canv = canvases.active_canvas();
        auto dest_mat = (!in_place) ?
            paste_matrix(canv.cursor_pos(), clipboard_topology) :
            std::optional<sm::matrix>{};
        if (dest_mat) {
            clipboard_topology.apply( *dest_mat );
        }

        auto& project = main_wnd.project();
        if (character) {
            sm::artwork artwork;
            sm::object_id character_root_bone;
            if (payload.contains("artwork_package")) {
                if (!payload["artwork_package"].is_string()) return;
                auto encoded = QByteArray::fromBase64(QByteArray::fromStdString(payload["artwork_package"].get<std::string>()),
                    QByteArray::AbortOnBase64DecodingErrors);
                sm::project resources;
                if (resources.deserialize({reinterpret_cast<const std::uint8_t*>(encoded.constData()), std::size_t(encoded.size())}) != sm::project_result::success ||
                    std::ranges::distance(resources.characters()) != 1) return;
                const auto copied_character=*resources.characters().begin();
                artwork = copied_character->artwork();
                character_root_bone = copied_character->character_root_bone();
            }
            auto pasted = project.paste_character(clipboard_topology, payload["name"].get<std::string>(), artwork, character_root_bone);
            if (!pasted) QMessageBox::warning(&main_wnd, "Paste Character", "Cannot paste this character.");
            return;
        }
        project.replace_skeletons(
            {},
            clipboard_topology.skeletons() | r::to<std::vector<sm::skel_ref>>()
        );
    }
    void cut_or_copy(ui::stick_man& main_wnd, bool should_cut) {
        QClipboard* clipboard = QApplication::clipboard();

        auto bytes = cut_or_copy_selection(main_wnd,
            should_cut ? selection_operation::cut : selection_operation::copy);
        if (bytes.isEmpty()) return; // Cancel leaves both project and clipboard unchanged.
        QMimeData* mime_data = new QMimeData;
        mime_data->setData(
            k_stickman_mime_type,
            bytes
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
