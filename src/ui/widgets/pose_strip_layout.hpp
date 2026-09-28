#pragma once
#include "../../core/sm_animation.hpp"
#include <QRectF>
#include <QString>

namespace ui {
// Derived presentation geometry. Cards occupy pixels, but no animation time.
class pose_strip_layout {
public:
    struct card {
        sm::object_id id;
        QRectF rect;
        double time;
    };
    struct transition {
        sm::object_id id;
        QRectF rect;
        double start;
        double duration;
    };
    struct position {
        std::size_t current_pose;
        double x;
        bool at_keyframe;
    };

    explicit pose_strip_layout(const sm::animation* animation = nullptr,
        double card_width = 150.0);
    std::vector<card> cards;
    std::vector<transition> transitions;
    double width = 300;
    static constexpr int height = 136;
    std::optional<position> at_time(double seconds) const;
    // Inverse presentation mapping. Card pixels map to their exact key time;
    // transition pixels map linearly across that transition.
    std::optional<double> time_at_x(double x) const;
    bool same_timing(const pose_strip_layout& other) const;
};

QString pose_keyframe_label(const sm::animation& animation, std::size_t index);
QString transition_duration_label(double seconds);
}
