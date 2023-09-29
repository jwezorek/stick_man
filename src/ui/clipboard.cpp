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
    
    std::tuple<sm::world, std::vector<std::string>> split_skeleton_into_selected_components(
            const sm::skeleton& skel, const bone_and_node_set& selection) {
        /*
        bone_and_node_set visited;

        for (node_or_bone model : selection.items()) {

        }
       
        sm::world output;
        return { std::move(output), {} };
         */
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
