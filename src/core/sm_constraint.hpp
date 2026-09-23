#pragma once
#include "sm_types.hpp"
#include "sm_object_id.hpp"
#include "json_fwd.hpp"
#include <map>
#include <string>

namespace sm {
    enum class rotation_reference_kind { world, parent, bone };
    struct rotation_reference {
        rotation_reference_kind kind = rotation_reference_kind::world;
        object_id bone_id;
        static rotation_reference world() { return {}; }
        static rotation_reference parent() { return {rotation_reference_kind::parent, {}}; }
        static rotation_reference bone(object_id id) { return {rotation_reference_kind::bone, id}; }
        bool operator==(const rotation_reference&) const = default;
    };
    struct rotation_constraint {
        object_id target_bone;
        rotation_reference reference;
        angle_range allowed;
    };
    struct rigid_triangle_constraint {
        object_id first_bone, second_bone;
        double relative_angle;
    };
    using constraint_definition = std::variant<rotation_constraint, rigid_triangle_constraint>;
    class constraint {
        friend class project;
        object_id id_;
        std::string name_;
        constraint_definition definition_;
        void set_name(std::string name) { name_ = std::move(name); }
    public:
        constraint(object_id id, std::string name, constraint_definition definition)
            : id_(id), name_(std::move(name)), definition_(std::move(definition)) {}
        const object_id& id() const noexcept { return id_; }
        const std::string& name() const noexcept { return name_; }
        const constraint_definition& definition() const noexcept { return definition_; }
        const rotation_constraint* rotation() const { return std::get_if<rotation_constraint>(&definition_); }
        const rigid_triangle_constraint* triangle() const { return std::get_if<rigid_triangle_constraint>(&definition_); }
        bool references(object_id bone) const;
        constraint remapped(const std::unordered_map<object_id, object_id>& ids) const;
    };
    using const_constraint_ref = ref<const constraint>;
    using expected_constraint = std::expected<const_constraint_ref, result>;
    // A semantic snapshot travels alongside scratch geometry, never inside bones.
    using constraint_map = std::map<object_id, constraint>;
    result validate_constraints(const topology&, const constraint_map&);
    nlohmann::json constraints_to_json(const constraint_map&);
    constraint_map constraints_from_json(const nlohmann::json&);

    // Adapter for the existing world/parent editor. No bone-owned state.
    std::optional<rot_constraint> editor_rotation_constraint(const bone&);
    result set_editor_rotation_constraint(bone&, double start, double span, bool parent);
    void remove_editor_rotation_constraint(bone&);
}
