#pragma once

#include "sm_object_id.hpp"
#include "sm_types.hpp"
#include "sm_artwork.hpp"
#include "sm_animation.hpp"

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

/*------------------------------------------------------------------------------------------------*/

namespace sm {

    class rig {
        friend class project;
    private:
        std::reference_wrapper<project> owner_;
        std::vector<object_id> skeleton_ids_;

        explicit rig(project& owner);
        void add_skeleton(const object_id& id);

    public:
        rig(const rig&) = delete;
        rig& operator=(const rig&) = delete;
        rig(rig&&) = default;
        rig& operator=(rig&&) = delete;

        std::size_t size() const noexcept;
        bool empty() const noexcept;
        bool contains(const object_id& id) const;
        const std::vector<object_id>& skeleton_ids() const noexcept;
        std::vector<const_skel_ref> skeletons() const;
    };

    class character : public detail::enable_protected_make_unique<character> {
        friend class project;
    private:
        const object_id id_;
        std::string name_;
        std::reference_wrapper<project> owner_;
        sm::rig rig_;
        object_id character_root_bone_;
        sm::artwork artwork_;
        animation_assets animation_data_;

    protected:
        character(project& owner, object_id id, std::string name, sm::rig&& rig,
            object_id character_root_bone = {});
        void set_name(const std::string& name);
        void set_character_root_bone(object_id id) noexcept;

    public:
        character(const character&) = delete;
        character& operator=(const character&) = delete;
        character(character&&) = delete;
        character& operator=(character&&) = delete;

        const object_id& id() const noexcept;
        std::string name() const;
        const project& owner() const noexcept;
        const sm::rig& rig() const noexcept;
        const object_id& character_root_bone() const noexcept { return character_root_bone_; }
        const animation_assets& animation_data() const noexcept { return animation_data_; }
        const sm::artwork& artwork() const noexcept { return artwork_; }
    };

}
