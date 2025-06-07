#include "sm_animation.h"
#include "sm_skeleton.h"
#include <ranges>

namespace r = std::ranges;
namespace rv = std::ranges::views;

/*------------------------------------------------------------------------------------------------*/

namespace {

    struct animation_event_visitor {
        sm::skeleton& skeleton;
        int curr_time;
        int timeslice_duration;
        int total_duration;

        animation_event_visitor(
                sm::skeleton& skel, int current_time, int timeslice_dur, int total_dur) :
            skeleton(skel),
            curr_time(current_time),
            timeslice_duration(timeslice_dur),
            total_duration(total_dur)
        { }

        void operator()(const sm::rotation& rot) {
            auto axis = skeleton.get_by_name<sm::node>(rot.axis);
            auto rotor = skeleton.get_by_name<sm::node>(rot.rotor);
            if (!axis || !rotor) {
                throw std::runtime_error("bad rotation event");
            }
            auto lead_bone = sm::find_bone_from_u_to_v(*axis, *rotor);
            if (!lead_bone) {
                throw std::runtime_error("bad rotation event");
            }
            auto& bone = lead_bone->get();
            auto percent_of_full_event =
                static_cast<double>(timeslice_duration) / static_cast<double>(total_duration);
            auto angle_delta = percent_of_full_event * rot.theta;
            bone.rotate_by(angle_delta, axis, false);
        }

        void operator()(const sm::translation& trans) {

        }
    };

    int next_event_boundary_time(
            const std::vector<sm::animation_event>& events,
            int curr_time,
            int duration) {

        std::vector<std::array<int, 2>> boundaries = events | rv::transform(
                [](const auto& ev) {
                    return std::array{ ev.start_time, ev.start_time + ev.duration };
                }
            ) | r::to<std::vector>();

        auto times = rv::join(
                boundaries
            ) | rv::filter(
                [=](int t) {
                    return t > curr_time && t <= curr_time + duration;
                }
            ) | r::to<std::vector>();
        times.push_back(curr_time + duration);

        return r::min(times);
    }

    void perform_action_on_skeleton(
            sm::skeleton& skel, const sm::animation_action& action, 
            int time, int timeslice_duration, int total_duration) {
        animation_event_visitor visitor(skel, time, timeslice_duration, total_duration);
        std::visit( visitor, action);
    }
}

sm::animation::animation(const std::string& name) : name_(name) {}

std::string sm::animation::name() const {
    return name_;
}

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

int sm::animation::duration() const {
    auto [interval, last_event] = timeline_.back();
    return last_event.start_time + last_event.duration;
}

void sm::animation::perform_timestep(skeleton& skel, int ts_start_time, int ts_duration) const {

    auto events = events_in_range(ts_start_time, ts_duration);
    auto t = ts_start_time;
    auto end_time = std::min(ts_start_time + ts_duration, duration());
    int time_remaining = ts_duration;

    while (time_remaining > 0) {
        int next_t = next_event_boundary_time(events, t, time_remaining);
        int time_slice = next_t - t;
        for (const auto& event : events) {
            auto event_end_time = event.start_time + event.duration;
            if (event.start_time <= t && t < event_end_time) {
                perform_action_on_skeleton(skel, event.action, t, time_slice, event.duration);
            }
        }
        t = next_t;
        time_remaining = end_time - t;
    }

}

sm::baked_animation sm::animation::bake(const skeleton& skel, int time_step) const {
    return baked_animation(0, {});
}

sm::baked_animation::baked_animation(int timestep, const std::vector<pose>& poses) : 
        time_step_{ timestep }, poses_at_time_{ poses } {
    if (time_step_ <= 0) {
        throw std::invalid_argument("time_step must be positive");
    }
}

/*------------------------------------------------------------------------------------------------*/

sm::pose sm::baked_animation::interpolate(const sm::pose& from, const sm::pose& to, double t) {

    if (from.size() != to.size()) {
        throw std::invalid_argument("pose size mismatch in interpolation");
    }

    return rv::zip(from, to) | rv::transform(
            [t](const auto& pair) {
                const auto& [p0, p1] = pair;
                return point{
                    p0.x * (1.0 - t) + p1.x * t,
                    p0.y * (1.0 - t) + p1.y * t
                };
            }
        ) | r::to<std::vector<point>>();
}

sm::pose sm::baked_animation::operator[](int time) const {

    if (poses_at_time_.empty()) {
        throw std::out_of_range("animation is empty");
    }

    int max_time = static_cast<int>(poses_at_time_.size() - 1) * time_step_;
    if (time <= 0) {
        return poses_at_time_.front();
    }
    if (time >= max_time) {
        return poses_at_time_.back();
    }

    int index = time / time_step_;
    int remainder = time % time_step_;

    if (remainder == 0) {
        return poses_at_time_[index];
    }

    double t = static_cast<double>(remainder) / time_step_;
    return interpolate(poses_at_time_[index], poses_at_time_[index + 1], t);
}