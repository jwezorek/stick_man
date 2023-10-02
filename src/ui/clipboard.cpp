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


    void copy_connected_component(sm::world& dest, auto& root,
            const bone_and_node_set& selection, bone_and_node_set& visited) {

        bool is_selected = selection.contains(std::ref(root));
        auto is_part_of_component = [&](const auto& itm)->bool {
                return is_selected == selection.contains(std::ref(itm));
            };

        auto node_visitor = [&](sm::node& node)->sm::visit_result {
                if (!is_part_of_component(node)) {
                    return sm::visit_result::terminate_branch;
                }
                visited.insert(node);

                if (world_contains<sm::node>(dest, node.name())) {
                    return sm::visit_result::continue_traversal;
                }

                

                return sm::visit_result::continue_traversal;
            };

        auto bone_visitor = [&](sm::bone& bone)->sm::visit_result {
                if (!is_part_of_component(bone)) {
                    return sm::visit_result::terminate_branch;
                }
                visited.insert(bone);

                return sm::visit_result::continue_traversal;
            };

        sm::dfs(
            root,
            node_visitor,
            bone_visitor,
            false
        );
    }

    std::vector<std::string> selected_skeletons(const sm::world& world,
            const bone_and_node_set& selection) {
        return {};
    }
    
    std::tuple<sm::world, std::vector<std::string>> split_skeleton_into_selected_components(
            const sm::skeleton& skel, const bone_and_node_set& selection) {
        
        bone_and_node_set visited;

        sm::world output;
        for (node_or_bone itm : selection.items()) {
            if (visited.contains_node_or_bone(itm)) {
                continue;
            }
            std::visit(
                [&](auto itm_ref) {
                    copy_connected_component(output, itm_ref.get(), selection, visited);
                },
                itm
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
