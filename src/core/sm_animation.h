#pragma once
#include "sm_types.h"
#include <string>
#include <variant>
#include <map>

namespace sm {

    struct rotation {
        std::string axis;
        std::string rotor;
        double theta;
        int duration;
    };

    struct translation {
        std::string subject;
        point offset;
        int duration;
    };

    template <typename T>
    concept is_animation_event = std::same_as<T, sm::rotation> || std::same_as<T, sm::translation>;

    using animation_event = std::variant<rotation, translation>;

    class animation {
        friend class skeleton;

        std::string name_;
        std::map<int, std::vector<animation_event>> timeline_;

    public:

        animation(const std::string& name);

        void insert(int start_time, const animation_event& event);
        void set(int start_time, const animation_event& event, int index);
        std::vector<animation_event> events(int start_time) const;
    };

}