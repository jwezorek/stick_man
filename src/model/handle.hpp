#pragma once

#include <ranges>
#include <variant>
#include <vector>

#include "../core/sm_object_id.hpp"
#include "../core/sm_project.hpp"
#include "../core/sm_skeleton.hpp"
#include "../core/sm_types.hpp"

namespace mdl {

    using const_skel_piece =
        std::variant<sm::const_node_ref, sm::const_bone_ref, sm::const_skel_ref>;
    using skel_piece = std::variant<sm::node_ref, sm::bone_ref, sm::skel_ref>;

    using model_object = sm::project_object;
    using const_model_object = sm::const_project_object;

    // All live model objects share one project-global object_id namespace.
    // A handle therefore needs no topology/component context of its own.
    using handle = sm::object_id;

    inline handle to_handle(sm::node& piece) { return piece.id(); }
    inline handle to_handle(const sm::node& piece) { return piece.id(); }
    inline handle to_handle(sm::bone& piece) { return piece.id(); }
    inline handle to_handle(const sm::bone& piece) { return piece.id(); }
    inline handle to_handle(sm::skeleton& piece) { return piece.id(); }
    inline handle to_handle(const sm::skeleton& piece) { return piece.id(); }

    inline handle to_handle(sm::node_ref piece) { return piece.get().id(); }
    inline handle to_handle(sm::const_node_ref piece) { return piece.get().id(); }
    inline handle to_handle(sm::bone_ref piece) { return piece.get().id(); }
    inline handle to_handle(sm::const_bone_ref piece) { return piece.get().id(); }
    inline handle to_handle(sm::skel_ref piece) { return piece.get().id(); }
    inline handle to_handle(sm::const_skel_ref piece) { return piece.get().id(); }
    inline handle to_handle(const skel_piece& piece) {
        return std::visit(
            [](auto ref) -> handle {
                return ref->id();
            },
            piece
        );
    }

    inline handle to_handle(const const_skel_piece& piece) {
        return std::visit(
            [](auto ref) -> handle {
                return ref->id();
            },
            piece
        );
    }

    auto to_handles(auto ptrs) {
        return ptrs |
            std::ranges::views::transform(
                [](auto* ptr) -> handle {
                    return ptr->id();
                }
            ) |
            std::ranges::to<std::vector<handle>>();
    }

}
