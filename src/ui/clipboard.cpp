#include "clipboard.h"
#include "../core/sm_skeleton.h"
#include "canvas_item.h"
#include "canvas.h"
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

    std::string make_unique_name(const std::string& base_name, const std::vector<std::string>& names) {
        auto used_names = names | r::to<std::unordered_set<std::string>>();
        int i = 0;
        std::string new_name;
        do {
            new_name = base_name + std::to_string(++i);
        } while (used_names.contains(new_name));
        return new_name;
    }

    sm::skeleton* create_skeleton(sm::world& dest, const std::string& skel_name) {
        std::string name = skel_name;
        if (dest.contains_skeleton_name(name)) {
            name = make_unique_name(skel_name, dest.skeleton_names());
        }
        auto skel = dest.create_skeleton(name);
        return &(skel->get());
    }

    void copy_connected_component(sm::world& dest, auto& root,
            const bone_and_node_set& selection, bone_and_node_set& copied) {

        bool is_selected = selection.contains(std::ref(root));
        auto is_part_of_component = [&](const auto& itm)->bool {
                return is_selected == selection.contains(std::ref(itm));
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
    
    void split_skeleton_by_selection(
            const sm::skeleton& skel, const bone_and_node_set& selection,
            sm::world& unselected, sm::world& selected) {
        
        bone_and_node_set copied;
        auto skeleton_pieces = topological_sort_nodes_and_bones(skel);

        for (node_or_bone part : skeleton_pieces) {
            if (copied.contains_node_or_bone(part)) {
                continue;
            }
            auto& dest = selection.contains_node_or_bone(part) ? selected : unselected;
            std::visit(
                [&](auto itm_ref) {
                    copy_connected_component(dest, itm_ref.get(), selection, copied);
                },
                part
            );
        }
        
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
    sm::world unselected;
    sm::world selected;
    split_skeleton_by_selection(skel, sel_set, unselected, selected);
    auto str = unselected.to_json() + "\n\n" + selected.to_json();
    to_text_file("C:\\test\\clippy.txt", str);
}
