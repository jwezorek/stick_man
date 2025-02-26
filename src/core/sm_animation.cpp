#include "sm_animation.h"

/*------------------------------------------------------------------------------------------------*/

sm::animation::animation(const std::string& name) : name_(name) {

}

void sm::animation::insert(int start_time, const animation_event& event) {
    timeline_[start_time].push_back(event);
}

void sm::animation::set(int start_time, const animation_event& event, int index) {
    timeline_.at(start_time).at(index) = event;
}

std::vector<sm::animation_event> sm::animation::events(int start_time) const {
    return timeline_.at(start_time);
}


