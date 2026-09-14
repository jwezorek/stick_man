#pragma once

#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>
#include <memory>
#include "sm_skeleton.hpp"
#include "sm_character.hpp"

/*------------------------------------------------------------------------------------------------*/

namespace sm {

    using mutable_project_object = std::variant<node_ref, bone_ref>;
    using const_project_object = std::variant<const_node_ref, const_bone_ref, const_skel_ref, const_character_ref>;
    using project_buffer = std::vector<std::uint8_t>;

    struct topology_change {
        std::vector<object_id> removed_skeleton_ids;
        std::vector<object_id> added_skeleton_ids;
        result status = result::success;
    };

    // Semantic command state lives beside, never inside, scratch topology.
    struct character_state {
        object_id id;
        std::string name;
        sm::artwork artwork;
    };
    struct membership_state {
        std::unordered_map<object_id, std::optional<object_id>> parents;
        std::vector<character_state> characters;
    };
    struct replacement_plan {
        membership_state membership;
        std::vector<object_id> deleted_character_ids;
    };

    enum class project_result {
        success,
        invalid_archive,
        missing_project_json,
        invalid_project_json,
        duplicate_object_id,
        archive_error,
        invalid_artwork
    };

    class project {
        using mutable_object = std::variant<node_ref, bone_ref, skel_ref, character_ref>;
        using character_tbl = std::unordered_map<object_id, std::unique_ptr<sm::character>>;

        // Keep characters alive until after topology destruction so a surviving skeleton can
        // never outlive the character referenced by its non-owning parent link.
        character_tbl characters_;
        sm::topology topology_;
        mutable std::unordered_map<object_id, mutable_object> objects_;
        mutable bool object_index_dirty_ = true;
        std::size_t next_character_name_ = 1;

        void invalidate_object_index() noexcept;
        bool rebuild_object_index();
        bool ensure_object_index() const;
        const mutable_object& get_mutable(const object_id& id) const;
        void detach_skeleton(skeleton& skel);
        void prune_empty_characters();

    public:
        project() = default;
        project(project&&) = delete;
        project& operator=(project&&) = delete;
        project(const project&) = delete;
        project& operator=(const project&) = delete;

        const sm::topology& topology() const;

        skeleton& create_skeleton(const point& pt);
        expected_skel copy_skeleton(
            const skeleton& source,
            const std::unordered_map<object_id, object_id>& id_remap = {});
        result delete_skeleton(const object_id& id);
        result can_create_bone(const node& u, const node& v) const;
        expected_bone create_bone(const std::string& name, node& u, node& v);
        expected_bone create_bone(object_id id, const std::string& name, node& u, node& v);
        topology_change replace_skeletons(
            const std::vector<object_id>& replacees,
            const std::vector<skel_ref>& replacements,
            const std::unordered_set<object_id>& regenerate_ids = {},
            const membership_state* restored_membership = nullptr);
        membership_state snapshot_membership(const std::vector<object_id>& skeletons) const;
        // Restore affected membership without replacing topology (e.g. adoption undo).
        result restore_membership(const membership_state& state);
        std::expected<replacement_plan, result> plan_replacement(
            const std::vector<object_id>& replacees,
            const std::vector<skel_ref>& replacements,
            const membership_state* restored_membership = nullptr) const;
        bool has_consistent_membership() const;

        expected_const_character create_character(std::span<const const_skel_ref> skeletons);
        result adopt_skeletons(const object_id& character_id, std::span<const const_skel_ref> skeletons);
        result remove_character(const object_id& id);
        expected_const_character character(const object_id& id) const;
        sm::artwork& artwork(const object_id& character_id);
        const sm::artwork& artwork(const object_id& character_id) const;
        bool slot_resolved(const object_id& character_id, const std::string& slot) const;
        auto characters() const { return detail::to_range_view<const_character_ref>(characters_); }

        // Mutable lookup accepts nodes and bones only; use const lookup for aggregate objects.
        mutable_project_object get(const object_id& id);
        const_project_object get(const object_id& id) const;
        void rename(object_id id, std::string name);
        bool has_unique_object_ids() const;

        void clear();

        std::expected<project_buffer, project_result> serialize() const;
        project_result deserialize(std::span<const std::uint8_t> buffer);
    };

}
