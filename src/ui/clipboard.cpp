#include "clipboard.h"
#include "../core/sm_skeleton.h"
#include "canvas_item.h"
#include "canvas.h"
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

    using node_or_bone = std::variant<sm::node_ref, sm::bone_ref>;

    class bone_and_node_set {
        std::unordered_map<void*, bool> node_map_;
        std::vector<sm::node*> nodes_;


        template<typename T>
        static void* to_void_star(const T& v) {
            return reinterpret_cast<void*>(
                const_cast<T*>(&v)
            );
        }

        template<typename T>
        static std::reference_wrapper<T> from_void_star(void* ptr) {
            T* model_ptr = reinterpret_cast<T*>(ptr);
            return std::ref(*model_ptr);
        }

    public:
        bone_and_node_set() {};

        bool contains(const sm::bone& b) const {
            return node_map_.contains(to_void_star(b));
        }

        bool contains(const sm::node& n) const {
            return node_map_.contains(to_void_star(n));
        }

        bool contains_node_or_bone(node_or_bone nb) const {
            return std::visit(
                [this](auto itm_ref)->bool {
                    return this->contains(itm_ref.get());
                },
                nb
            );
        }

        void insert(const sm::bone& b) {
            node_map_[to_void_star(b)] = false;
        }

        void insert(const sm::node& n) {
            node_map_[to_void_star(n)] = true;
        }

        auto items() const {
            return node_map_ | rv::transform(
                [](const auto& pair)->node_or_bone {
                    return (pair.second) ?
                        node_or_bone{ from_void_star<sm::node>(pair.first) } :
                        node_or_bone{ from_void_star<sm::bone>(pair.first) };
                }
            );
        }
    };

    template<typename T>
    bool world_contains(const sm::world& w, const std::string& str) {
        for (auto skel : w.skeletons()) {
            if (skel.get().contains<T>(str)) {
                return true;
            }
        }
        return false;
    }

    std::optional<sm::node_ref> get_named_node(const sm::world& w, const std::string& str) {
        for (auto skel : w.skeletons()) {
            auto node = skel.get().get_by_name<sm::node>(str);
            if (node) {
                return node;
            }
        }
        return {};
    }

    sm::node_ref copy_node(sm::world& dest, sm::node& node) {
        auto pos = node.world_pos();
        auto skel_ref = dest.create_skeleton(pos.x, pos.y);
        auto& skel = skel_ref.get();
        auto copy = skel.root_node();
        skel.set_name(copy.get(), node.name());
        return copy;
    }

    void copy_connected_component(sm::world& dest, auto& root,
            const bone_and_node_set& selection, bone_and_node_set& copied) {

        bool is_selected = selection.contains(std::ref(root));
        auto is_part_of_component = [&](const auto& itm)->bool {
                return is_selected == selection.contains(std::ref(itm));
            };

        auto node_visitor = [&](sm::node& node)->sm::visit_result {
                if (!is_part_of_component(node)) {
                    return sm::visit_result::terminate_branch;
                }
                copied.insert(node);

                if (world_contains<sm::node>(dest, node.name())) {
                    return sm::visit_result::continue_traversal;
                }

                copy_node(dest, node);

                return sm::visit_result::continue_traversal;
            };

        auto bone_visitor = [&](sm::bone& bone)->sm::visit_result {
                if (!is_part_of_component(bone)) {
                    return sm::visit_result::terminate_branch;
                }
                copied.insert(bone);
                if (world_contains<sm::bone>(dest, bone.name())) {
                    // this should not happen.
                    return sm::visit_result::continue_traversal;
                }

                auto u = get_named_node(dest, bone.parent_node().name());
                if (!u) {
                    u = copy_node(dest, bone.parent_node());
                }

                auto v = get_named_node(dest, bone.child_node().name());
                if (!v) {
                    v = copy_node(dest, bone.child_node());
                }

                dest.create_bone(bone.name(), u->get(), v->get());

                return sm::visit_result::continue_traversal;
            };

        sm::dfs(
            root,
            node_visitor,
            bone_visitor,
            true
        );
    }

    std::vector<std::string> selected_skeletons(const sm::world& world,
            const bone_and_node_set& selection) {
        return {};
    }

    //TODO: make this const correct...
    std::vector<node_or_bone> topological_sort_nodes_and_bones(const sm::skeleton& skel) {
        std::vector<node_or_bone> sorted;
        sm::dfs(
            const_cast<sm::skeleton&>(skel).root_node().get(),
            [&]( sm::node& node)->sm::visit_result {
                sorted.push_back(std::ref(node));
                return sm::visit_result::continue_traversal;
            },
            [&]( sm::bone& bone)->sm::visit_result {
                sorted.push_back(std::ref(bone));
                return sm::visit_result::continue_traversal;
            },
            true
        );
        return sorted;
    }

    bone_and_node_set bone_and_node_set_from_canvas(ui::canvas& canv) {
        auto sel = canv.selection();
        bone_and_node_set set;
        for (auto* itm : sel) {
            auto node_ptr = dynamic_cast<ui::node_item*>(itm);
            if (node_ptr) {
                set.insert(node_ptr->model());
                continue;
            }
            auto bone_ptr = dynamic_cast<ui::bone_item*>(itm);
            if (bone_ptr) {
                set.insert(bone_ptr->model());
                continue;
            }
            throw std::runtime_error("bone_and_node_set_from_canvas: skeleton in selection");
        }
        return set;
    }
    
    std::tuple<sm::world, std::vector<std::string>> split_skeleton_into_selected_components(
            const sm::skeleton& skel, const bone_and_node_set& selection) {
        
        bone_and_node_set copied;
        auto skeleton_pieces = topological_sort_nodes_and_bones(skel);

        sm::world output;
        for (node_or_bone part : skeleton_pieces) {
            if (copied.contains_node_or_bone(part)) {
                continue;
            }
            std::visit(
                [&](auto itm_ref) {
                    copy_connected_component(output, itm_ref.get(), selection, copied);
                },
                part
            );
        }
        
        return { std::move(output), selected_skeletons(output, selection)};
    }
}

QByteArray ui::cut_selection(canvas& canv)
{
    return QByteArray();
}

QByteArray ui::copy_selection(canvas& canv)
{
    return QByteArray();
}

void ui::paste_selection(canvas& canv, const QByteArray& bytes)
{
}

void ui::debug(canvas& canv)
{
    std::vector<skeleton_item*> skel_items = canv.skeleton_items();
    auto& skel = skel_items.front()->model();
    auto sel_set = bone_and_node_set_from_canvas(canv);
    auto [world, selected_skel_names] = split_skeleton_into_selected_components(skel, sel_set);
    auto test = world.to_json();
}
