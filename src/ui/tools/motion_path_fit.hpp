#pragma once
#include "../../core/sm_animation.hpp"
#include <span>
#include <vector>

namespace ui::tool {
    // Editor-side gesture fitting. Input samples are model/canvas positions; output
    // is displacement geometry beginning at {0,0}.
    sm::motion_path fit_motion_path(std::span<const sm::point> samples,
        sm::motion_path_kind kind, double tolerance = 2.0);
    sm::motion_path convert_motion_path(const sm::motion_path& path,
        sm::motion_path_kind kind);
}
