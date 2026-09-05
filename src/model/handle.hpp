#pragma once
#include <variant>
#include "../core/sm_types.hpp"
#include "../core/sm_skeleton.hpp"
#include "../core/sm_object_id.hpp"

namespace mdl {
    using const_skel_piece =
        std::variant<sm::const_node_ref, sm::const_bone_ref, sm::const_skel_ref>;
    using skel_piece = std::variant<sm::node_ref, sm::bone_ref, sm::skel_ref>;
    struct handle {
    private:
        template<sm::is_node_or_bone T>
        std::expected<sm::ref<T>, sm::result> to_aux(sm::world& world) const {
            auto piece = world.get<T>(object_id);
            if (!piece) {
                return std::unexpected(sm::result::not_found);
            }
            return *piece;
        }
        sm::expected_skel to_skeleton(sm::world& world) const;
    public:
        // Skeleton handles use skeleton_id. Node/bone handles use only object_id;
        // their component skeleton may change after topology edits.
        sm::object_id skeleton_id;
        sm::object_id object_id;
        bool operator==(const handle& hand) const = default;
        template<sm::is_skel_piece T>
        T& to(sm::world& world) const {
            if constexpr (std::is_same_v<T, sm::skeleton>) {
                auto skel = to_skeleton(world);
                if (!skel) {
                    throw std::runtime_error("unable to resolve skeleton handle");
                }
                return skel->get();
            } else {
                auto piece = to_aux<T>(world);
                if (!piece) {
                    throw std::runtime_error("unable to resolve skeleton-piece handle");
                }
                return piece->get();
            }
        }
    };
    struct handle_hash {
        size_t operator()(const handle& hand) const noexcept;
    };
    handle to_handle(const skel_piece& piece);
    auto to_handles(auto ptrs) {
        return ptrs | std::views::transform(
            [](auto ptr)->handle {
                return to_handle(sm::ref(*ptr));
            }
        ) | std::ranges::to<std::vector<handle>>();
    }
}
