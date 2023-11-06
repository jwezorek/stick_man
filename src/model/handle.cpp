#include "handle.h"
#include <boost/functional/hash.hpp>
#include <variant>
#include "../core/sm_skeleton.h"
#include "../core/sm_bone.h"

/*------------------------------------------------------------------------------------------------*/

namespace r = std::ranges;
namespace rv = std::ranges::views;

namespace{

    template<class... Ts> struct overload : Ts... { using Ts::operator()...; };

}

bool mdl::handle::operator==(const handle& hand) const
{
    return this->skel_name == hand.skel_name && this->piece_name == hand.piece_name;
}

size_t mdl::handle_hash::operator()(const handle& hand) const
{
    size_t seed = 0;
    boost::hash_combine(seed, hand.skel_name);
    boost::hash_combine(seed, hand.piece_name);
    return seed;
}

mdl::handle mdl::to_handle(const mdl::skel_piece& piece) {
    return std::visit(
        overload{
            [](sm::is_node_or_bone_ref auto node_or_bone)->handle {
                auto skel_name = node_or_bone.get().owner().get().name();
                return { skel_name, node_or_bone.get().name() };
            } ,
            [](sm::skel_ref itm)->handle {
                auto& skel = itm.get();
                return {skel.name(), {}};
            }
        },
        piece
    );
}