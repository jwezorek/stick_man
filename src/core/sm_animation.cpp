#include "sm_animation.h"
#include <ranges>

namespace r = std::ranges;
namespace rv = std::ranges::views;

/*------------------------------------------------------------------------------------------------*/

namespace {

    struct animation_event_visitor {
        int curr_time;
        int duration;
        sm::skeleton& skeleton;

        animation_event_visitor(sm::skeleton& skel, int time, int dur) :
            skeleton(skel),
            curr_time(time),
            duration(dur)
        { }

        void operator()(const sm::rotation& rot) {

        }

        void operator()(const sm::translation& trans) {

        }
    };

    int next_event_transition_time(
            const std::vector<sm::animation_event>& events, int curr_time, int duration) {
        auto end_times = events | rv::transform(
            [](auto&& event) {
                return event.start_time + event.duration;
            }
        ) | r::to<std::vector>();
        end_times.push_back(curr_time + duration);
        return r::min(end_times);
    }

    void perform_action_on_skeleton(
            sm::skeleton& skel, const sm::animation_action& action, int time, int duration) {
        animation_event_visitor visitor(skel, time, duration);
        std::visit( visitor, action);
    }
}

sm::animation::animation(const std::string& name) : name_(name) {}

void sm::animation::insert(const sm::animation_event& event) {
    auto overlapping = timeline_.query_overlapping(
        { event.start_time, event.start_time + event.duration }
    );
    std::vector<sm::animation_event> same_start;

    for (auto& [iv, ev] : overlapping) {
        if (ev.start_time == event.start_time)
            same_start.push_back(ev);
    }

    std::sort(same_start.begin(), same_start.end(), 
            [](const sm::animation_event& a, const sm::animation_event& b) {
            return a.index < b.index;
        }
    );

    if (event.index < -1 || event.index > static_cast<int>(same_start.size())) {
        throw std::invalid_argument("insert: index out of bounds for given start_time");
    }

    sm::animation_event ev = event;

    if (event.index == -1) {
        ev.index = static_cast<int>(same_start.size());
    } else {
        if (event.index < static_cast<int>(same_start.size()) &&
            same_start[event.index].index != event.index) {
            throw std::invalid_argument("insert: non-contiguous index for given start_time");
        }

        for (auto& e : same_start | std::views::reverse) {
            if (e.index >= event.index) {
                timeline_.remove({ e.start_time, e.start_time + e.duration }, e);
                ++e.index;
                timeline_.insert({ e.start_time, e.start_time + e.duration }, e);
            }
        }
    }

    timeline_.insert({ ev.start_time, ev.start_time + ev.duration }, ev);
}

void sm::animation::set(
        int start_time, int index, int duration, const sm::animation_action& action) {

    auto overlapping = timeline_.query_overlapping({ start_time, start_time + 1 });

    for (auto& [iv, ev] : overlapping) {
        if (ev.start_time == start_time && ev.index == index) {
            timeline_.remove(iv, ev);
            ev.duration = duration;
            ev.action = action;
            timeline_.insert({ ev.start_time, ev.start_time + ev.duration }, ev);
            return;
        }
    }

    throw std::out_of_range("set: no such animation_event at given start_time and index");
}

void sm::animation::move(int start_time, int index, int new_index) {
    auto overlapping = timeline_.query_overlapping({ start_time, start_time + 1 });

    std::vector<sm::animation_event> same_start;
    for (auto& [iv, ev] : overlapping) {
        if (ev.start_time == start_time)
            same_start.push_back(ev);
    }

    std::sort(same_start.begin(), same_start.end(), 
        [](const sm::animation_event& a, const sm::animation_event& b) {
            return a.index < b.index;
        }
    );

    if (index < 0 || index >= static_cast<int>(same_start.size()) ||
        new_index < 0 || new_index >= static_cast<int>(same_start.size())) {
        throw std::out_of_range("move: index or new_index out of range");
    }

    sm::animation_event moving = same_start[index];

    for (auto& ev : same_start) {
        timeline_.remove({ ev.start_time, ev.start_time + ev.duration }, ev);
    }

    same_start.erase(same_start.begin() + index);
    same_start.insert(same_start.begin() + new_index, moving);

    for (int i = 0; i < static_cast<int>(same_start.size()); ++i) {
        same_start[i].index = i;
        timeline_.insert(
            { same_start[i].start_time, same_start[i].start_time + same_start[i].duration }, 
            same_start[i]
        );
    }
}

std::vector<sm::animation_event> sm::animation::events() const {
    std::vector<sm::animation_event> result;
    for (const auto& [iv, ev] : timeline_.entries()) {
        result.push_back(ev);
    }

    std::sort(result.begin(), result.end(), 
        [](const sm::animation_event& a, const sm::animation_event& b) {
            return std::tie(a.start_time, a.index) < std::tie(b.start_time, b.index);
        }
    );

    return result;
}

std::vector<sm::animation_event> sm::animation::events_at_time(int start_time) const {
    return events_in_range(start_time, 1);
}

std::vector<sm::animation_event> sm::animation::events_in_range(int start_time, int duration) const {
    std::vector<sm::animation_event> result;
    for (const auto& [iv, ev] : timeline_.query_overlapping({ start_time, start_time + duration })) {
        result.push_back(ev);
    }

    std::sort(result.begin(), result.end(), 
        [](const sm::animation_event& a, const sm::animation_event& b) {
            return std::tie(a.start_time, a.index) < std::tie(b.start_time, b.index);
        }
    );

    return result;
}

void sm::animation::perform_timestep(skeleton& skel, int start_time, int duration) const {

    auto events = events_in_range(start_time, duration);
    auto t = start_time;
    auto end_time = start_time + duration;
    int time_remaining = duration;

    while (time_remaining > 0) {
        int next_t = next_event_transition_time(events, t, time_remaining);
        int time_slice = next_t - t;
        for (const auto& event : events) {
            if (t >= event.start_time && t < next_t) {
                perform_action_on_skeleton(skel, event.action, t, time_slice);
            }
        }
        t = next_t;
        time_remaining = end_time - t;
    }
}
