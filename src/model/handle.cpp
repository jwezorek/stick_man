#include "handle.hpp"
#include <variant>
#include "../core/sm_skeleton.hpp"
#include "../core/sm_bone.hpp"

namespace {
    template<class... Ts> struct overload : Ts... { using Ts::operator()...; };

    void hash_combine(std::size_t& seed, std::size_t value) noexcept {
        seed ^= value + static_cast<std::size_t>(0x9e3779b97f4a7c15ull) + (seed << 6) + (seed >> 2);
    }
}

sm::expected_skel mdl::handle::to_skeleton(sm::world& world) const {
    return world.skeleton(skeleton_id);
}

size_t mdl::handle_hash::operator()(const handle& hand) const noexcept {
    std::size_t seed = std::hash<sm::object_id>{}(hand.skeleton_id);
    hash_combine(seed, std::hash<sm::object_id>{}(hand.object_id));
    return seed;
}

mdl::handle mdl::to_handle(const mdl::skel_piece& piece) {
    return std::visit(
        overload{
            [](sm::is_node_or_bone_ref auto node_or_bone)->handle {
                return {node_or_bone->owner().id(), node_or_bone->id()};
            },
            [](sm::skel_ref skel)->handle {
                return {skel->id(), {}};
            }
        },
        piece
    );
}
