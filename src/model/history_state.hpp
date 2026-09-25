#pragma once

#include <cstdint>

namespace mdl {

    // Tracks document identity through undo/redo independently of stack depth.
    // Revision values are never reused within one document lifetime, so branching
    // after an undo cannot accidentally compare equal to an abandoned saved state.
    class history_state {
    public:
        using revision = std::uint64_t;

        struct transition {
            revision before = 0;
            revision after = 0;
        };

        [[nodiscard]] constexpr bool dirty() const noexcept {
            return current_ != saved_;
        }

        [[nodiscard]] constexpr transition advance() noexcept {
            const transition result{current_, next_++};
            current_ = result.after;
            return result;
        }

        constexpr void undo(const transition& value) noexcept {
            current_ = value.before;
        }

        constexpr void redo(const transition& value) noexcept {
            current_ = value.after;
        }

        constexpr void mark_saved() noexcept {
            saved_ = current_;
        }

        constexpr void reset_clean() noexcept {
            current_ = 0;
            saved_ = 0;
            next_ = 1;
        }

    private:
        revision current_ = 0;
        revision saved_ = 0;
        revision next_ = 1;
    };

} // namespace mdl
