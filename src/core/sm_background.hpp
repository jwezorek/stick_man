#pragma once

#include "sm_artwork.hpp"
#include "sm_image.hpp"
#include "sm_object_id.hpp"
#include <string>

namespace sm {
    // Project-level raster reference image. Its transform is expressed directly in
    // canvas/world coordinates; it is never character- or bone-relative.
    struct background_image {
        object_id id;
        std::string name;
        image_resource image;
        sprite_transform transform;
    };
}
