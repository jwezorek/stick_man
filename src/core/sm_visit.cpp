#include <stack>
#include <unordered_set>
#include "sm_visit.h"
#include "sm_bone.h"

namespace r = std::ranges;
namespace rv = std::ranges::views;

/*------------------------------------------------------------------------------------------------*/

namespace {

    template<class... Ts> struct overloaded : Ts... { using Ts::operator()...; };
    template<class... Ts> overloaded(Ts...) -> overloaded<Ts...>;

    template <typename T>
    using visitor_fn = std::function<sm::visit_result(T&)>;

    template <typename V, typename E>
    class node_or_bone_visitor {
        visitor_fn<V> node_visit_;
        visitor_fn<E> bone_visit_;
    public:
        node_or_bone_visitor(const visitor_fn<V>& j, visitor_fn<E>& b) :
            node_visit_(j),
            bone_visit_(b) {
        }

        sm::visit_result operator()(std::reference_wrapper<V> j_ref) {
            return (node_visit_) ?
                node_visit_(j_ref) :
                sm::visit_result::continue_traversal;
        }

        sm::visit_result operator()(std::reference_wrapper<E> b_ref) {
            return (bone_visit_) ?
                bone_visit_(b_ref) :
                sm::visit_result::continue_traversal;
        }
    };

    template<typename V, typename E>
    class node_or_bone_neighbors_visitor {
        bool downstream_;
    public:
        node_or_bone_neighbors_visitor(bool downstream) :
            downstream_(downstream)
        {}

        std::vector<sm::node_or_bone<V, E>> operator()(std::reference_wrapper<V> j_ref) {
            auto& node = j_ref.get();
            auto neighbors = node.child_bones() |
                rv::transform(
                    [](auto child)->sm::node_or_bone<V, E> {
                        return child;
                    }
            ) | r::to<std::vector<sm::node_or_bone<V, E>>>();
            if (!downstream_) {
                if (node.parent_bone().has_value()) {
                    neighbors.push_back(node.parent_bone().value());
                }
            }
            return neighbors;
        }

        std::vector<sm::node_or_bone<V, E>> operator()(std::reference_wrapper<E> b_ref) {
            auto& bone = b_ref.get();
            std::vector<sm::node_or_bone<V, E>> neighbors;
            if (!downstream_) {
                neighbors.push_back(std::ref(bone.parent_node()));
            }
            neighbors.push_back(std::ref(bone.child_node()));
            return neighbors;
        }
    };

    template<typename V, typename E>
    using stack_item = sm::node_or_bone<V, E>;

    template<typename V, typename E>
    uint64_t get_id(const sm::node_or_bone<V, E>& var) {
        auto ptr = std::visit(
            []<typename T>(std::reference_wrapper<T> val_ref)->void* {
            auto& val = val_ref.get();
            return reinterpret_cast<void*>(
                const_cast<std::remove_cv<T>::type*>(&val)
                );
        },
            var
        );
        return reinterpret_cast<uint64_t>(ptr);
    }

    uint64_t get_id(const sm::bone& var) {
        return reinterpret_cast<uint64_t>(&var);
    }

    uint64_t get_id(const sm::node& var) {
        return reinterpret_cast<uint64_t>(&var);
    }

    template<typename V, typename E>
    void dfs_aux(sm::node_or_bone<V, E> root, visitor_fn<V> visit_node,
            visitor_fn<E> visit_bone, bool just_downstream) {

        std::stack<stack_item<V, E>> stack;
        std::unordered_set<uint64_t> visited;
        node_or_bone_visitor<V, E> visitor(visit_node, visit_bone);
        node_or_bone_neighbors_visitor<V, E> neighbors_visitor(just_downstream);
        stack.push( root );

        while (!stack.empty()) {
            auto item = stack.top();
            stack.pop();
            auto id = get_id(item);
            if (visited.contains(id)) {
                continue;
            }
            auto result = std::visit(visitor, item);
            visited.insert(id);
            if (result == sm::visit_result::terminate_traversal) {
                return;
            }
            else if (result == sm::visit_result::terminate_branch) {
                continue;
            }

            auto neighbors = std::visit(neighbors_visitor, item);
            for (const auto& neighbor : neighbors) {
                stack.push(neighbor);
            }
        }
    }

    struct hierarchy_item {
        sm::maybe_bone_ref prev;
        sm::bone_ref curr;
    };

    struct hierarchical_traversal_state {
        sm::maybe_const_node_ref src_node;
        std::unordered_set<sm::bone*> visited;

        hierarchical_traversal_state(const sm::node& src) :
            src_node(src)
        {}
    };

    std::vector<sm::bone_ref> bone_hierarchy_neighbors(
            sm::bone& bone, hierarchical_traversal_state& state) {
        auto neighbors = bone.child_bones();
        auto parent = bone.parent_bone();
        if (parent) {
            neighbors.push_back(*parent);
        } else {
            r::copy(bone.sibling_bones(), std::back_inserter(neighbors));
        }
        return neighbors |
            rv::filter(
                [&](auto neighbor_ref) {
                    return !state.visited.contains(&neighbor_ref.get());
                }
        ) | r::to<std::vector<sm::bone_ref>>();
    }
}

void sm::visit_nodes_and_bones(node& root, node_visitor visit_node, bone_visitor visit_bone, bool just_downstream) {
    dfs_aux<sm::node, sm::bone>(
        std::ref(root),
        visit_node,
        visit_bone,
        just_downstream
    );
}

void sm::visit_nodes_and_bones(bone& root, node_visitor visit_node, bone_visitor visit_bone, bool just_downstream) {
    dfs_aux<sm::node, sm::bone>(
        std::ref(root),
        visit_node,
        visit_bone,
        just_downstream
    );
}

void sm::visit_nodes_and_bones(const node& root, const_node_visitor visit_node, const_bone_visitor visit_bone, 
        bool just_downstream) {
    dfs_aux<const sm::node, const sm::bone>(
        std::ref(root),
        visit_node,
        visit_bone,
        just_downstream
    );
}

void sm::visit_nodes_and_bones(const bone& root, const_node_visitor visit_node,
        const_bone_visitor visit_bone, bool just_downstream) {
    dfs_aux<const sm::node, const sm::bone>(
        std::ref(root),
        visit_node,
        visit_bone,
        just_downstream
    );
}

void sm::visit_nodes(node& j, node_visitor visit_node, bool just_downstream) {
    visit_nodes_and_bones(j, visit_node, {}, just_downstream);
}

void sm::visit_bones(node& j, bone_visitor visit_bone, bool just_downstream) {
    visit_nodes_and_bones(j, {}, visit_bone, just_downstream);
}

void sm::visit_bones(bone& b, bone_visitor visit_bone, bool just_downstream) {
    visit_bone(b);
    visit_bones(b.child_node(), visit_bone, just_downstream);
}

void sm::visit_bone_hierarchy(node& src, bone_visitor_with_prev visit) {
    std::stack<hierarchy_item> stack;
    for (auto bone : src.adjacent_bones()) {
        stack.push({ std::nullopt, bone });
    }

    hierarchical_traversal_state state(src);
    while (!stack.empty()) {
        auto item = stack.top();
        stack.pop();

        if (state.visited.contains(&item.curr.get())) {
            continue;
        }

        auto result = visit(item.prev, item.curr);
        state.visited.insert(&item.curr.get());

        if (result == sm::visit_result::terminate_traversal) {
            return;
        } else if (result == sm::visit_result::terminate_branch) {
            continue;
        }

        for (const auto& neighbor : bone_hierarchy_neighbors(item.curr, state)) {
            stack.push({ item.curr, neighbor });
        }
    }
}

