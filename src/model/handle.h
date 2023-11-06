#pragma once

#include <string>
#include <variant>
#include "../core/sm_types.h"

namespace mdl {

    using const_skel_piece =
        std::variant<sm::const_node_ref, sm::const_bone_ref, sm::const_skel_ref>;
    using skel_piece = std::variant<sm::node_ref, sm::bone_ref, sm::skel_ref>;

    struct handle {
        std::string skel_name;
        std::string piece_name;

        bool operator==(const handle& hand) const;
    };

    struct handle_hash {
        size_t operator()(const handle& hand) const;
    };

    handle to_handle(const skel_piece& piece);

}