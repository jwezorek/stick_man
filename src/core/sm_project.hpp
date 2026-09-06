#pragma once

#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>
#include "sm_skeleton.hpp"

/*------------------------------------------------------------------------------------------------*/

namespace sm {

    using project_object = std::variant<node_ref, bone_ref, skel_ref>;
    using const_project_object = std::variant<const_node_ref, const_bone_ref, const_skel_ref>;
    using project_buffer = std::vector<std::uint8_t>;

    enum class project_result {
        success,
        invalid_archive,
        missing_project_json,
        invalid_project_json,
        duplicate_object_id,
        archive_error
    };

    class project {
        topology world_;
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

        sm::topology& world();
        const sm::topology& world() const;

        project_object get(const object_id& id);
        const_project_object get(const object_id& id) const;
        bool has_unique_object_ids() const;

        void clear();

        std::expected<project_buffer, project_result> serialize() const;
        project_result deserialize(std::span<const std::uint8_t> buffer);
    };

}
