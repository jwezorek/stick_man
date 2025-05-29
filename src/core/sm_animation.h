#pragma once

#include <string>
#include <variant>
#include <vector>
#include <stdexcept>
#include <algorithm>
#include "sm_types.h"
#include "sm_interval_tree.h"

/*------------------------------------------------------------------------------------------------*/

namespace sm {

    struct rotation {
        std::string axis;
        std::string rotor;
        double theta;

        friend bool operator==(const rotation&, const rotation&) = default;
    };

    struct translation {
        std::string subject;
        point offset;

        friend bool operator==(const translation&, const translation&) = default;
    };

    template <typename T>
    concept is_animation_action =
        std::same_as<T, rotation> || std::same_as<T, translation>;

    using animation_action = std::variant<rotation, translation>;

    struct animation_event {
        int start_time;
        int duration;
        int index; // this event's order in all events starting at start_time
        animation_action action;

        friend bool operator==(const animation_event&, const animation_event&) = default;
    };

    using time_interval = sm::interval<int>;

    class skeleton;

    class animation {
        friend class skeleton;

        std::string name_;
        sm::interval_tree<int, animation_event> timeline_;

    public:
        animation(const std::string& name);

        std::string name() const;
        void insert(const animation_event& event);
        void set(int start_time, int index, int duration, const animation_action& action);
        void move(int start_time, int index, int new_index);
        std::vector<animation_event> events() const;
        std::vector<animation_event> events_at_time(int start_time) const;
        std::vector<animation_event> events_in_range(int start_time, int duration) const;

        void perform_timestep(skeleton& skel, int time, int duration) const;
    };

}