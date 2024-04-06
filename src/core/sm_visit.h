#pragma once

#include "sm_types.h"
#include <functional>

/*------------------------------------------------------------------------------------------------*/

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
    using bone_visitor_with_prev = std::function<visit_result(maybe_bone_ref, bone&)>;

    void visit_nodes_and_bones(node& root, node_visitor visit_node = {}, bone_visitor visit_bone = {},
        bool just_downstream = false);
    void visit_nodes_and_bones(bone& root, node_visitor visit_node = {}, bone_visitor visit_bone = {},
        bool just_downstream = false);
    void visit_nodes_and_bones(const node& root, const_node_visitor visit_node = {},
        const_bone_visitor visit_bone = {}, bool just_downstream = false);
    void visit_nodes_and_bones(const bone& root, const_node_visitor visit_node = {},
        const_bone_visitor visit_bone = {}, bool just_downstream = false);
    void visit_nodes(node& j, node_visitor visit_node, bool just_downstream = true);
    void visit_bones(node& j, bone_visitor visit_node, bool just_downstream = true);
    void visit_bones(bone& b, bone_visitor visit_bone, bool just_downstream = true);

    void visit_bone_hierarchy(node& src, bone_visitor_with_prev visit);
}