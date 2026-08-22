#include "sm_object_id.hpp"

#include <algorithm>
#include <random>

namespace {

    std::mt19937_64 make_generator() {
        std::random_device rd;
        std::seed_seq seed{
            rd(), rd(), rd(), rd(),
            rd(), rd(), rd(), rd()
        };
        return std::mt19937_64(seed);
    }

    int hex_value(char c) {
        if (c >= '0' && c <= '9') {
            return c - '0';
        }
        if (c >= 'a' && c <= 'f') {
            return 10 + c - 'a';
        }
        if (c >= 'A' && c <= 'F') {
            return 10 + c - 'A';
        }
        return -1;
    }

}

sm::object_id::object_id(storage_type bytes) noexcept :
    bytes_(bytes) {
}

sm::object_id sm::object_id::generate() {
    thread_local std::mt19937_64 generator = make_generator();

    storage_type bytes;
    for (std::size_t offset = 0; offset < bytes.size(); offset += 8) {
        auto value = generator();
        for (std::size_t i = 0; i < 8; ++i) {
            bytes[offset + i] = static_cast<std::uint8_t>(value >> (i * 8));
        }
    }
    return object_id(bytes);
}

std::expected<sm::object_id, std::string> sm::object_id::from_string(std::string_view str) {
    std::array<char, 32> hex{};
    std::size_t count = 0;

    for (char c : str) {
        if (c == '-') {
            continue;
        }
        if (count == hex.size()) {
            return std::unexpected("object ID is too long");
        }
        if (hex_value(c) < 0) {
            return std::unexpected("object ID contains a non-hexadecimal character");
        }
        hex[count++] = c;
    }

    if (count != hex.size()) {
        return std::unexpected("object ID must contain exactly 32 hexadecimal digits");
    }

    storage_type bytes;
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        auto high = hex_value(hex[i * 2]);
        auto low = hex_value(hex[i * 2 + 1]);
        bytes[i] = static_cast<std::uint8_t>((high << 4) | low);
    }
    return object_id(bytes);
}

std::string sm::object_id::to_string() const {
    static constexpr char digits[] = "0123456789abcdef";

    std::string result;
    result.resize(bytes_.size() * 2);
    for (std::size_t i = 0; i < bytes_.size(); ++i) {
        result[i * 2] = digits[bytes_[i] >> 4];
        result[i * 2 + 1] = digits[bytes_[i] & 0x0f];
    }
    return result;
}

const sm::object_id::storage_type& sm::object_id::bytes() const noexcept {
    return bytes_;
}

bool sm::object_id::is_nil() const noexcept {
    return std::ranges::all_of(bytes_, [](std::uint8_t byte) { return byte == 0; });
}

std::size_t std::hash<sm::object_id>::operator()(const sm::object_id& id) const noexcept {
    // FNV-1a is sufficient here: the IDs are already random and this keeps hashing
    // independent of platform endianness and alignment.
    std::size_t value = sizeof(std::size_t) == 8
        ? static_cast<std::size_t>(14695981039346656037ull)
        : static_cast<std::size_t>(2166136261u);
    const std::size_t prime = sizeof(std::size_t) == 8
        ? static_cast<std::size_t>(1099511628211ull)
        : static_cast<std::size_t>(16777619u);

    for (auto byte : id.bytes()) {
        value ^= byte;
        value *= prime;
    }
    return value;
}
