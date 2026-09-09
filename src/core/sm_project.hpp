#pragma once

#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>
#include "sm_skeleton.hpp"

/*------------------------------------------------------------------------------------------------*/

namespace sm {

    using project_object = std::variant<node_ref, bone_ref, skel_ref>;
    using const_project_object = std::variant<const_node_ref, const_bone_ref, const_skel_ref>;
    using project_buffer = std::vector<std::uint8_t>;

    struct topology_change {
        std::vector<object_id> removed_skeleton_ids;
        std::vector<object_id> added_skeleton_ids;
    };

    enum class project_result {
        success,
        invalid_archive,
        missing_project_json,
        invalid_project_json,
        duplicate_object_id,
        archive_error
    };

    class project {
        sm::topology topology_;
        mutable std::unordered_map<object_id, project_object> objects_;
        mutable bool object_index_dirty_ = true;

        void invalidate_object_index() noexcept;
        bool rebuild_object_index();
        bool ensure_object_index() const;

    public:
        project() = default;
        project(project&&) = delete;
        project& operator=(project&&) = delete;
        project(const project&) = delete;
        project& operator=(const project&) = delete;

        sm::topology& topology();
        const sm::topology& topology() const;

        skeleton& create_skeleton(const point& pt);
        expected_skel copy_skeleton(
            const skeleton& source,
            const std::unordered_map<object_id, object_id>& id_remap = {});
        result delete_skeleton(const object_id& id);
        expected_bone create_bone(const std::string& name, node& u, node& v);
        expected_bone create_bone(object_id id, const std::string& name, node& u, node& v);
        topology_change replace_skeletons(
            const std::vector<object_id>& replacees,
            const std::vector<skel_ref>& replacements,
            const std::unordered_set<object_id>& regenerate_ids = {});

        project_object get(const object_id& id);
        const_project_object get(const object_id& id) const;
        bool has_unique_object_ids() const;

        void clear();

        std::expected<project_buffer, project_result> serialize() const;
        project_result deserialize(std::span<const std::uint8_t> buffer);
    };

}
