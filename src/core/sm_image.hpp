#pragma once
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace sm {
    using image_buffer = std::vector<std::uint8_t>;
    struct pixel_rect { int x, y, width, height; };

    // Immutable, top-down RGBA8 with straight alpha. Copies share storage; a region
    // keeps its backing alive. Row access works identically for sheets and imports.
    // Invalid input throws std::invalid_argument; no operation accesses files.
    class image_resource {
        std::shared_ptr<const image_buffer> pixels_;
        std::size_t offset_ = 0;
        int width_ = 0, height_ = 0, stride_ = 0;
    public:
        static constexpr int max_dimension = 8192;
        static image_resource decode(std::span<const std::uint8_t> encoded);
        static image_resource from_rgba(int width, int height, image_buffer pixels);
        image_resource region(pixel_rect rect) const;
        int width() const noexcept { return width_; }
        int height() const noexcept { return height_; }
        std::span<const std::uint8_t> row(int y) const;
        image_buffer encode_png() const;
    };
}
