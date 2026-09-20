#include "sm_character.hpp"
#include "sm_project.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

/*------------------------------------------------------------------------------------------------*/

sm::rig::rig(project& owner) : owner_(owner) {}

void sm::rig::add_skeleton(const object_id& id) {
    skeleton_ids_.push_back(id);
}

std::size_t sm::rig::size() const noexcept { return skeleton_ids_.size(); }
bool sm::rig::empty() const noexcept { return skeleton_ids_.empty(); }

bool sm::rig::contains(const object_id& id) const {
    return std::ranges::find(skeleton_ids_, id) != skeleton_ids_.end();
}

const std::vector<sm::object_id>& sm::rig::skeleton_ids() const noexcept {
    return skeleton_ids_;
}

std::vector<sm::const_skel_ref> sm::rig::skeletons() const {
    std::vector<const_skel_ref> resolved;
    resolved.reserve(skeleton_ids_.size());
    for (const auto& id : skeleton_ids_) {
        auto skel = owner_.get().topology().skeleton(id);
        if (!skel) {
            throw std::runtime_error("character rig contains a missing skeleton");
        }
        resolved.push_back(*skel);
    }
    return resolved;
}

/*------------------------------------------------------------------------------------------------*/

sm::character::character(project& owner, object_id id, std::string name, sm::rig&& rig,
        object_id character_root_bone) :
    id_(id), name_(std::move(name)), owner_(owner), rig_(std::move(rig)),
    character_root_bone_(character_root_bone) {}

void sm::character::set_name(const std::string& name) { name_ = name; }
void sm::character::set_character_root_bone(object_id id) noexcept { character_root_bone_ = id; }

const sm::object_id& sm::character::id() const noexcept { return id_; }
std::string sm::character::name() const { return name_; }
const sm::project& sm::character::owner() const noexcept { return owner_.get(); }
const sm::rig& sm::character::rig() const noexcept { return rig_; }
