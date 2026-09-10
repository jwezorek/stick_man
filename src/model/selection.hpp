#pragma once
#include "handle.hpp"
#include <unordered_map>
#include <unordered_set>
#include <algorithm>
#include <type_traits>

namespace mdl {
    // Resolve semantic selection to topology without changing the selection itself.
    inline selection selection_topology(const selection& selected) {
        std::unordered_map<sm::object_id, selection_object> pieces;
        auto skeleton = [&](sm::const_skel_ref skel) {
            for (auto node : skel->nodes()) pieces.emplace(node->id(), node);
            for (auto bone : skel->bones()) pieces.emplace(bone->id(), bone);
        };
        for (const auto& object : selected) std::visit([&](auto ref) {
            using T = std::remove_cvref_t<decltype(ref.get())>;
            if constexpr (std::is_same_v<T, sm::character>) {
                for (auto skel : ref->rig().skeletons()) skeleton(skel);
            } else if constexpr (std::is_same_v<T, sm::skeleton>) skeleton(ref);
            else pieces.emplace(ref->id(), ref);
        }, object);
        selection result;
        for (const auto& [id, object] : pieces) result.push_back(object);
        return result;
    }

    // Only canvas inference uses this normalization. Explicit pane selection is
    // already semantic and must bypass it, even for a one-component character.
    inline selection infer_selection(const selection& selected) {
        auto topology = selection_topology(selected);
        if (topology.empty()) return {};
        std::unordered_set<sm::object_id> ids;
        std::unordered_map<sm::object_id, sm::const_skel_ref> skeletons;
        for (const auto& object : topology) std::visit([&](auto ref) {
            using T = std::remove_cvref_t<decltype(ref.get())>;
            if constexpr (std::is_same_v<T, sm::node> || std::is_same_v<T, sm::bone>) {
                ids.insert(ref->id());
                skeletons.emplace(ref->owner().id(), ref->owner());
            }
        }, object);
        for (const auto& [id, skel] : skeletons) {
            for (auto node : skel->nodes()) if (!ids.contains(node->id())) return topology;
            for (auto bone : skel->bones()) if (!ids.contains(bone->id())) return topology;
        }
        auto parent = skeletons.begin()->second->parent_character();
        if (parent && parent->get().rig().size() == skeletons.size() &&
            std::ranges::all_of(skeletons, [&](const auto& pair) { return parent->get().rig().contains(pair.first); }))
            return {sm::const_character_ref(parent->get())};
        selection result;
        for (const auto& [id, skel] : skeletons) result.push_back(skel);
        return result;
    }
}
