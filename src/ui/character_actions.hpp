#pragma once
#include "canvas/scene.hpp"

namespace ui::character_actions {
    void make(canvas::scene& canvas, mdl::project& project);
    void adopt(canvas::scene& canvas, mdl::project& project, QWidget* parent);
}
