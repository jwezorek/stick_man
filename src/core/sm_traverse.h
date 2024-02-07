#pragma once

#include "sm_types.h"
#include <functional>

namespace sm {

    enum class visit_result {
        continue_traversal,
        terminate_branch,
        terminate_traversal
    };

    using node_visitor = std::function<visit_result(node&)>;
    using bone_visitor = std::function<visit_result(bone&)>;
    using const_node_visitor = std::function<visit_result(const node&)>;
    using const_bone_visitor = std::function<visit_result(const bone&)>;
    using node_visitor_with_prev = std::function<visit_result(maybe_node_ref, node&)>;
    using bone_visitor_with_prev = std::function<visit_result(maybe_bone_ref, bone&)>;
    using const_node_visitor_with_prev =
        std::function<visit_result(maybe_const_node_ref, const node&)>;
    using const_bone_visitor_with_prev =
        std::function<visit_result(maybe_const_bone_ref, const bone&)>;

    void dfs(node& root, node_visitor visit_node = {}, bone_visitor visit_bone = {},
        bool just_downstream = false);
    void dfs(bone& root, node_visitor visit_node = {}, bone_visitor visit_bone = {},
        bool just_downstream = false);
    void dfs(const node& root, const_node_visitor visit_node = {},
        const_bone_visitor visit_bone = {}, bool just_downstream = false);
    void dfs(const bone& root, const_node_visitor visit_node = {},
        const_bone_visitor visit_bone = {}, bool just_downstream = false);

    void visit_nodes(node& j, node_visitor visit_node, bool just_downstream = true);
    void visit_bones(node& j, bone_visitor visit_node, bool just_downstream = true);
    void visit_bones(bone& b, bone_visitor visit_bone, bool just_downstream = true);

    void dfs(node& root, node_visitor_with_prev visit_node = {},
        bone_visitor_with_prev visit_bone = {});
    void dfs(bone& root, node_visitor_with_prev visit_node = {},
        bone_visitor_with_prev visit_bone = {});
    void dfs(const node& root, const_node_visitor_with_prev visit_node = {},
        const_bone_visitor_with_prev visit_bone = {});
    void dfs(const bone& root, const_node_visitor_with_prev visit_node = {},
        const_bone_visitor_with_prev visit_bone = {});

    void visit_nodes(node& j, node_visitor_with_prev visit_node);
    void visit_bones(node& j, bone_visitor_with_prev visit_node);

}