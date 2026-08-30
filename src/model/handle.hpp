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
            auto skel = world.skeleton(skeleton_id);
            if (!skel) {
                return std::unexpected(skel.error());
            }
            auto piece = skel->get().get<T>(object_id);
            if (!piece) {
                return std::unexpected(sm::result::not_found);
            }
            return *piece;
        }

        sm::expected_skel to_skeleton(sm::world& world) const;

    public:
        sm::object_id skeleton_id;
        sm::object_id object_id;

        bool operator==(const handle& hand) const = default;

        template<sm::is_skel_piece T>
        T& to(sm::world& world) const {
            if constexpr (std::is_same_v<T, sm::skeleton>) {
                auto val = to_skeleton(world);
                if (!val) {
                    throw std::runtime_error("invalid handle to skeleton");
                }
                return val->get();
            } else {
                auto val = to_aux<T>(world);
                if (!val) {
                    throw std::runtime_error("invalid handle to node/bone");
                }
                return val->get();
            }
        }
    };

    struct handle_hash {
        size_t operator()(const handle& hand) const noexcept;
    };

    handle to_handle(const skel_piece& piece);

    auto to_handles(auto ptrs) {
        return ptrs |
            std::ranges::views::transform(
                [](auto* ptr)->handle {
                    return to_handle(sm::ref(*ptr));
                }
            );
    }

}
