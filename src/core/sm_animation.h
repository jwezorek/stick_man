#pragma once

#pragma once

#include "sm_types.h"
#include <string>
#include <unordered_map>
#include <vector>
#include <variant>
#include <map>
#include <optional>

namespace sm {

    struct rotation {
        std::string axis;
        std::string rotor;
        double theta;
        int duration_ms;
    };

    struct translation {
        std::string subject;
        point offset;
        int duration_ms;
    };

    template <typename T>
    concept is_animation_event = std::same_as<T, sm::rotation> || std::same_as<T, sm::translation>;

    using animation_event = std::variant<rotation, translation>;

    class pose {
    private:
        std::unordered_map<std::string, sm::point> node_positions_;

    public:
        pose() = default;

        void set_node_position(const std::string& node_name, const sm::point& pos);
        std::optional<sm::point> node_position(const std::string& node_name) const;

        void apply(skeleton& skel) const;
    };

    class animation;

    class animation_script {
    private:
        std::string name_;
        std::map<int, std::vector<animation_event>> timeline_;

    public:
        animation_script(const std::string& name);

        void insert(int start_time_ms, const animation_event& event);
        void set(int start_time_ms, const animation_event& event, int index);
        std::vector<animation_event> events(int start_time_ms) const;

        const std::string& name() const;

        animation bake(const skeleton& skel) const;
    };

    class animation {
    private:
        friend class animation_script;

        std::string name_;
        std::vector<std::pair<int, pose>> keyframes_;

        animation(const std::string& name);
        void add_keyframe_internal(int time_ms, const pose& p);

    public:
        pose pose_at_time(int time_ms) const;
        const std::string& name() const;
    };

} 