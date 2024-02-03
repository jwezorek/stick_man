#include <stack>
#include <unordered_set>
#include "sm_traverse.h"
#include "sm_bone.h"

namespace r = std::ranges;
namespace rv = std::ranges::views;

namespace {

    template<class... Ts> struct overloaded : Ts... { using Ts::operator()...; };
    template<class... Ts> overloaded(Ts...) -> overloaded<Ts...>;

    template <typename T>
    using visitor_fn = std::function<sm::visit_result(T&)>;

    template <typename V, typename T>
    using visitor_with_prev_fn =
        std::function<sm::visit_result(std::optional<std::reference_wrapper<V>>, T&)>;

    template <typename V, typename E>
    class node_or_bone_visitor {
        visitor_with_prev_fn<V, V> node_visit_;
        visitor_with_prev_fn<V, E> bone_visit_;
        std::optional<std::reference_wrapper<V>> prev_;
    public:
        node_or_bone_visitor(const visitor_with_prev_fn<V, V>& j, visitor_with_prev_fn<V, E>& b) :
            node_visit_(j),
            bone_visit_(b) {
        }

        void set_prev(std::optional<std::reference_wrapper<V>> prev) {
            prev_ = prev;
        }

        sm::visit_result operator()(std::reference_wrapper<V> j_ref) {
            return (node_visit_) ?
                node_visit_(prev_, j_ref) :
                sm::visit_result::continue_traversal;
        }

        sm::visit_result operator()(std::reference_wrapper<E> b_ref) {
            return (bone_visit_) ?
                bone_visit_(prev_, b_ref) :
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
    struct stack_item {
        std::optional<std::reference_wrapper<V>> prev;
        sm::node_or_bone<V, E> val;
    };

    template<typename V, typename E>
    std::reference_wrapper<V> get_prev(stack_item<V, E> item) {
        return std::visit(
            overloaded{
                [](std::reference_wrapper<V> j_ref) -> std::reference_wrapper<V> {
                    return j_ref;
                },
                [](std::reference_wrapper<E> b_ref) -> std::reference_wrapper<V> {
                    return b_ref.get().parent_node();
                }
            },
            item.val
        );
    }

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
    void dfs_aux(sm::node_or_bone<V, E> root, visitor_with_prev_fn<V, V> visit_node,
        visitor_with_prev_fn<V, E> visit_bone, bool just_downstream) {

        using item_t = stack_item<V, E>;
        std::stack<item_t> stack;
        std::unordered_set<uint64_t> visited;
        node_or_bone_visitor<V, E> visitor(visit_node, visit_bone);
        node_or_bone_neighbors_visitor<V, E> neighbors_visitor(just_downstream);
        stack.push(item_t{ std::nullopt,root });

        while (!stack.empty()) {
            auto item = stack.top();
            stack.pop();
            auto id = get_id(item.val);
            if (visited.contains(id)) {
                continue;
            }
            visitor.set_prev(item.prev);
            auto result = std::visit(visitor, item.val);
            visited.insert(id);
            if (result == sm::visit_result::terminate_traversal) {
                return;
            }
            else if (result == sm::visit_result::terminate_branch) {
                continue;
            }

            auto neighbors = std::visit(neighbors_visitor, item.val);
            for (const auto& neighbor : neighbors) {
                std::optional<std::reference_wrapper<V>> prev = { get_prev<V, E>(item) };
                stack.push(item_t{ prev, neighbor });
            }
        }
    }

    template<typename V, typename T>
    visitor_with_prev_fn<V, T> make_visit_fn_with_prev(visitor_fn<T> fn) {
        if (!fn) {
            return {};
        }
        return [fn](std::optional<std::reference_wrapper<V>> prev, T& v)->sm::visit_result {
            return fn(v);
            };
    }
}

void sm::dfs(node& root, node_visitor visit_node, bone_visitor visit_bone,
    bool just_downstream) {
    dfs_aux<sm::node, sm::bone>(
        std::ref(root),
        make_visit_fn_with_prev<sm::node, sm::node>(visit_node),
        make_visit_fn_with_prev<sm::node, sm::bone>(visit_bone),
        just_downstream
    );
}

void sm::dfs(bone& root, node_visitor visit_node, bone_visitor visit_bone,
    bool just_downstream) {
    dfs_aux<sm::node, sm::bone>(
        std::ref(root),
        make_visit_fn_with_prev<sm::node, sm::node>(visit_node),
        make_visit_fn_with_prev<sm::node, sm::bone>(visit_bone),
        just_downstream
    );
}

void sm::dfs(const node& root, const_node_visitor visit_node,
    const_bone_visitor visit_bone, bool just_downstream) {
    dfs_aux<const sm::node, const sm::bone>(
        std::ref(root),
        make_visit_fn_with_prev<const sm::node, const sm::node>(visit_node),
        make_visit_fn_with_prev<const sm::node, const sm::bone>(visit_bone),
        just_downstream
    );
}

void sm::dfs(const bone& root, const_node_visitor visit_node,
    const_bone_visitor visit_bone, bool just_downstream) {
    dfs_aux<const sm::node, const sm::bone>(
        std::ref(root),
        make_visit_fn_with_prev<const sm::node, const sm::node>(visit_node),
        make_visit_fn_with_prev<const sm::node, const sm::bone>(visit_bone),
        just_downstream
    );
}

void sm::visit_nodes(node& j, node_visitor visit_node) {
    dfs(j, visit_node, {}, true);
}

void sm::visit_bones(node& j, bone_visitor visit_bone) {
    dfs(j, {}, visit_bone, true);
}

void sm::visit_bones(bone& b, bone_visitor visit_bone) {
    visit_bone(b);
    visit_bones(b.child_node(), visit_bone);
}

void sm::dfs(node& root, node_visitor_with_prev visit_node,
    bone_visitor_with_prev visit_bone) {
    dfs_aux<sm::node, sm::bone>(
        std::ref(root),
        visit_node,
        visit_bone,
        false
    );
}

void sm::dfs(bone& root, node_visitor_with_prev visit_node,
    bone_visitor_with_prev visit_bone) {
    dfs_aux<sm::node, sm::bone>(
        std::ref(root),
        visit_node,
        visit_bone,
        false
    );
}

void sm::dfs(const node& root, const_node_visitor_with_prev visit_node,
    const_bone_visitor_with_prev visit_bone) {
    dfs_aux<const sm::node, const sm::bone>(
        std::ref(root),
        visit_node,
        visit_bone,
        false
    );
}

void sm::dfs(const bone& root, const_node_visitor_with_prev visit_node,
    const_bone_visitor_with_prev visit_bone) {
    dfs_aux<const sm::node, const sm::bone>(
        std::ref(root),
        visit_node,
        visit_bone,
        false
    );
}

void sm::visit_nodes(node& j, node_visitor_with_prev visit_node) {
    dfs(j, visit_node, bone_visitor_with_prev{});
}

void sm::visit_bones(node& j, bone_visitor_with_prev visit_bone) {
    dfs(j, node_visitor_with_prev{}, visit_bone);
}