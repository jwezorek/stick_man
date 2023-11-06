#pragma once

#include <string>
#include <variant>
#include "../core/sm_types.h"
#include "../core/sm_skeleton.h"

namespace mdl {

    using const_skel_piece =
        std::variant<sm::const_node_ref, sm::const_bone_ref, sm::const_skel_ref>;
    using skel_piece = std::variant<sm::node_ref, sm::bone_ref, sm::skel_ref>;

    struct handle {
    private:
        template<sm::is_node_or_bone T>
        std::expected<std::reference_wrapper<T>, sm::result> to_aux(
                sm::world& world) {
            auto skel = world.skeleton(skel_name);
            if (!skel) {
                return std::unexpected(skel.error());
            }

            auto maybe_piece = skel->get().get_by_name<T>(piece_name);
            if (!maybe_piece) {
                return std::unexpected(sm::result::not_found);
            }
            return *maybe_piece;
        }

        sm::expected_skel to_skeleton(sm::world& world);
    public:
        std::string skel_name;
        std::string piece_name;

        bool operator==(const handle& hand) const;

        template<sm::is_skel_piece T>
        T& to(sm::world& world) {
            if constexpr (std::is_same<T, sm::skeleton>::value) {
                auto val = to_skeleton(world);
                if (!val) {
                    throw std::runtime_error("invalid handle to skeleton");
                }
                return val->get();
            }
            else {
                auto val = to_aux<T>(world);
                if (!val) {
                    throw std::runtime_error("invalid handle to node/bone");
                }
                return val->get();
            }
        }
    };

    struct handle_hash {
        size_t operator()(const handle& hand) const;
    };

    handle to_handle(const skel_piece& piece);

    auto to_handles(auto ptrs) {
        return ptrs |
            std::ranges::views::transform(
                [](auto* ptr)->handle {
                    return to_handle(std::ref(*ptr));
                }
            );
    }

}