#pragma once
#include "sm_types.h"
#include "sm_object_id.hpp"
#include <string>
#include <variant>
#include <map>

namespace sm {

    struct rotation {
        object_id axis;
        object_id rotor;
        double theta;
        int duration;
    };

    struct translation {
        object_id subject;
        point offset;
        int duration;
    };

    template <typename T>
    concept is_animation_event = std::same_as<T, sm::rotation> || std::same_as<T, sm::translation>;

    using animation_event = std::variant<rotation, translation>;

    class animation {
        friend class skeleton;

        const object_id id_;
        std::string name_;
        std::map<int, std::vector<animation_event>> timeline_;

    public:
        animation(const std::string& name);
        animation(object_id id, const std::string& name);

        const object_id& id() const noexcept;
        const std::string& name() const noexcept;
        void insert(int start_time, const animation_event& event);
        void set(int start_time, const animation_event& event, int index);
        std::vector<animation_event> events(int start_time) const;
    };

}
