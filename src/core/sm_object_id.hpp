#pragma once

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <string>
#include <string_view>

namespace sm {

    class object_id {
    public:
        using storage_type = std::array<std::uint8_t, 16>;

        object_id() = default;
        explicit object_id(storage_type bytes) noexcept;

        static object_id generate();
        static std::expected<object_id, std::string> from_string(std::string_view str);

        std::string to_string() const;
        const storage_type& bytes() const noexcept;
        bool is_nil() const noexcept;

        auto operator<=>(const object_id&) const = default;

    private:
        storage_type bytes_{};
    };

}

namespace std {

    template<>
    struct hash<sm::object_id> {
        std::size_t operator()(const sm::object_id& id) const noexcept;
    };

}
