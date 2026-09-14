#include "sm_image.hpp"
#include <algorithm>
#include <climits>
#include <stdexcept>
#define STBI_NO_STDIO
#define STBI_MAX_DIMENSIONS 8192
#define STB_IMAGE_IMPLEMENTATION
#include "third-party/stb_image.h"
#define STBI_WRITE_NO_STDIO
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "third-party/stb_image_write.h"

namespace {
    void dimensions(int w, int h) {
        if (w <= 0 || h <= 0 || w > sm::image_resource::max_dimension || h > sm::image_resource::max_dimension)
            throw std::invalid_argument("Image dimensions must be between 1 and 8192 pixels.");
    }
}
sm::image_resource sm::image_resource::from_rgba(int w, int h, image_buffer pixels) {
    dimensions(w, h);
    if (pixels.size() != std::size_t(w) * h * 4) throw std::invalid_argument("Invalid RGBA buffer size.");
    image_resource result;
    result.width_ = w; result.height_ = h; result.stride_ = w * 4;
    result.pixels_ = std::make_shared<const image_buffer>(std::move(pixels));
    return result;
}
sm::image_resource sm::image_resource::decode(std::span<const std::uint8_t> bytes) {
    if (bytes.empty() || bytes.size() > INT_MAX) throw std::invalid_argument("Invalid encoded image buffer.");
    int w = 0, h = 0, channels = 0;
    if (!stbi_info_from_memory(bytes.data(), int(bytes.size()), &w, &h, &channels))
        throw std::invalid_argument("Unrecognized or damaged image.");
    dimensions(w, h);
    std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> decoded(
        stbi_load_from_memory(bytes.data(), int(bytes.size()), &w, &h, &channels, 4), stbi_image_free);
    if (!decoded) throw std::invalid_argument("Could not decode image.");
    return from_rgba(w, h, image_buffer(decoded.get(), decoded.get() + std::size_t(w) * h * 4));
}
sm::image_resource sm::image_resource::region(pixel_rect r) const {
    if (!pixels_ || r.x < 0 || r.y < 0 || r.width <= 0 || r.height <= 0 ||
        r.width > width_ || r.height > height_ || r.x > width_ - r.width || r.y > height_ - r.height)
        throw std::invalid_argument("Frame rectangle lies outside its image.");
    auto result = *this;
    result.offset_ += std::size_t(r.y) * stride_ + std::size_t(r.x) * 4;
    result.width_ = r.width; result.height_ = r.height;
    return result;
}
std::span<const std::uint8_t> sm::image_resource::row(int y) const {
    if (!pixels_ || y < 0 || y >= height_) throw std::out_of_range("Image row out of range.");
    return {pixels_->data() + offset_ + std::size_t(y) * stride_, std::size_t(width_) * 4};
}
sm::image_buffer sm::image_resource::encode_png() const {
    dimensions(width_, height_);
    int size = 0;
    // The stb memory encoder respects row stride, including shared sheet regions.
    std::unique_ptr<unsigned char, decltype(&std::free)> png(
        stbi_write_png_to_mem(row(0).data(), stride_, width_, height_, 4, &size), std::free);
    if (!png) throw std::runtime_error("Could not encode PNG.");
    return image_buffer(png.get(), png.get() + size);
}
