#include "sm_animation.h"

/*------------------------------------------------------------------------------------------------*/

sm::animation::animation(const std::string& name) :
    id_(object_id::generate()), name_(name) {
}

sm::animation::animation(object_id id, const std::string& name) :
    id_(id), name_(name) {
}

const sm::object_id& sm::animation::id() const noexcept { return id_; }
const std::string& sm::animation::name() const noexcept { return name_; }

void sm::animation::insert(int start_time, const animation_event& event) {
    timeline_[start_time].push_back(event);
}

void sm::animation::set(int start_time, const animation_event& event, int index) {
    timeline_.at(start_time).at(index) = event;
}

std::vector<sm::animation_event> sm::animation::events(int start_time) const {
    return timeline_.at(start_time);
}
