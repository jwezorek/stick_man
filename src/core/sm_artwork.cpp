#include "sm_artwork.hpp"
#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>
#define STB_RECT_PACK_IMPLEMENTATION
#include "third-party/stb_rect_pack.h"

namespace {
    void check(bool ok, const char* message) { if (!ok) throw std::invalid_argument(message); }
    void name_ok(const std::string& name) { check(!name.empty(), "Name cannot be empty."); }
    void finite(sm::point p) { check(std::isfinite(p.x) && std::isfinite(p.y), "Coordinates must be finite."); }
    template<class Map> void available(const Map& map, const std::string& name) {
        name_ok(name); check(!map.contains(name), "Name already exists.");
    }
    template<class Map> void rename(Map& map, const std::string& from, const std::string& to) {
        check(map.contains(from), "Name not found.");
        if (from == to) return;
        available(map, to);
        auto value = map.at(from);
        map.emplace(to, std::move(value)); map.erase(from);
    }
}
void sm::artwork::insert_frame(const std::string& name, std::span<const std::uint8_t> encoded) {
    available(frames_, name);
    insert_frame(name, sprite_frame{image_resource::decode(encoded), {}});
}
void sm::artwork::insert_frame(const std::string& name, sprite_frame frame) {
    available(frames_, name); finite(frame.registration_origin);
    check(frame.image.width() > 0 && frame.image.height() > 0, "Empty frame image.");
    check(frame.image.width() <= max_frame_dimension && frame.image.height() <= max_frame_dimension,
        "Sprite frames must be at most 8190 pixels per dimension, leaving room for page padding.");
    frames_.emplace(name, std::move(frame));
}
void sm::artwork::rename_frame(const std::string& name, const std::string& replacement) {
    rename(frames_, name, replacement);
    for (auto& [_, app] : appearances_) for (auto& slot : app.appearance_slots)
        for (auto& [state, frame] : slot.states) if (frame == name) frame = replacement;
}
void sm::artwork::delete_frame(const std::string& name) {
    check(frames_.contains(name), "Frame not found.");
    for (const auto& [_, app] : appearances_) for (const auto& slot : app.appearance_slots)
        for (const auto& [state, frame] : slot.states)
            check(frame != name, "Frame is referenced by an appearance. Change its mappings before deleting it.");
    frames_.erase(name);
}
void sm::artwork::set_registration_origin(const std::string& name, point origin) {
    finite(origin); frames_.at(name).registration_origin = origin;
}
void sm::artwork::add_slot(const std::string& name, slot_definition definition) {
    available(definitions_, name);
    check(definition.anchor == bone_anchor::root || definition.anchor == bone_anchor::tip, "Invalid anchor.");
    std::set<std::string> states;
    for (const auto& s : definition.states) { name_ok(s); check(states.insert(s).second, "Duplicate state."); }
    check(states.contains("default"), "Slot must declare default state.");
    definitions_.emplace(name, std::move(definition));
}
void sm::artwork::rename_slot(const std::string& name, const std::string& replacement) {
    rename(definitions_, name, replacement);
    for (auto& [_, app] : appearances_) for (auto& slot : app.appearance_slots) if (slot.slot == name) slot.slot = replacement;
}
void sm::artwork::delete_slot(const std::string& name) {
    check(definitions_.contains(name), "Slot not found.");
    definitions_.erase(name);
    for (auto& [_, app] : appearances_) std::erase_if(app.appearance_slots, [&](const auto& slot) { return slot.slot == name; });
}
void sm::artwork::bind_slot(const std::string& name, object_id bone, bone_anchor anchor) {
    check(anchor == bone_anchor::root || anchor == bone_anchor::tip, "Invalid anchor.");
    auto& slot = definitions_.at(name); slot.bone = bone; slot.anchor = anchor;
}
void sm::artwork::add_state(const std::string& slot, const std::string& state) {
    name_ok(state); auto& states = definitions_.at(slot).states;
    check(std::ranges::find(states, state) == states.end(), "State already exists."); states.push_back(state);
}
void sm::artwork::rename_state(const std::string& slot, const std::string& state, const std::string& replacement) {
    check(state != "default", "Cannot rename default state."); name_ok(replacement);
    auto& states = definitions_.at(slot).states;
    auto it = std::ranges::find(states, state); check(it != states.end(), "State not found.");
    if (state == replacement) return;
    check(std::ranges::find(states, replacement) == states.end(), "State already exists."); *it = replacement;
    for (auto& [_, app] : appearances_) for (auto& s : app.appearance_slots)
        if (s.slot == slot && s.states.contains(state)) rename(s.states, state, replacement);
}
void sm::artwork::delete_state(const std::string& slot, const std::string& state) {
    check(state != "default", "Cannot delete default state.");
    auto& states = definitions_.at(slot).states;
    check(std::ranges::find(states, state) != states.end(), "State not found."); std::erase(states, state);
    for (auto& [_, app] : appearances_) for (auto& s : app.appearance_slots) if (s.slot == slot) s.states.erase(state);
}
void sm::artwork::validate_appearance(const appearance& app) const {
    std::set<std::string> seen;
    for (const auto& slot : app.appearance_slots) {
        check(definitions_.contains(slot.slot), "Unknown slot definition.");
        check(seen.insert(slot.slot).second, "Duplicate appearance slot.");
        check(slot.states.contains("default"), "Appearance slot must map default state.");
        const auto& vocabulary = definitions_.at(slot.slot).states;
        for (const auto& [state, frame] : slot.states) {
            check(std::ranges::find(vocabulary, state) != vocabulary.end(), "Unknown semantic state.");
            check(!frame || frames_.contains(*frame), "Unknown frame target.");
        }
        finite(slot.transform.translation); finite(slot.transform.scale);
        check(std::isfinite(slot.transform.rotation), "Rotation must be finite.");
    }
}
void sm::artwork::add_appearance(const std::string& name, appearance value) {
    available(appearances_, name); validate_appearance(value); appearances_.emplace(name, std::move(value));
}
void sm::artwork::rename_appearance(const std::string& name, const std::string& replacement) { rename(appearances_, name, replacement); }
void sm::artwork::delete_appearance(const std::string& name) {
    check(appearances_.contains(name), "Appearance not found."); appearances_.erase(name);
}
void sm::artwork::set_appearance(const std::string& name, appearance value) {
    check(appearances_.contains(name), "Appearance not found."); validate_appearance(value); appearances_.at(name) = std::move(value);
}
sm::frame_target sm::artwork::resolve_frame(const std::string& app, const std::string& slot, const std::string& state) const {
    for (const auto& s : appearances_.at(app).appearance_slots) if (s.slot == slot) {
        auto it = s.states.find(state); return it == s.states.end() ? s.states.at("default") : it->second;
    }
    return std::nullopt;
}
void sm::artwork::remap_bones(const std::unordered_map<object_id, object_id>& remap) {
    for (auto& [_, slot] : definitions_) if (auto it = remap.find(slot.bone); it != remap.end()) slot.bone = it->second;
}
sm::packed_artwork sm::artwork::pack(int page_size, int padding) const {
    check(padding >= 1 && padding <= image_resource::max_dimension / 2, "Invalid page padding.");
    if (page_size == 0) {
        page_size = 2048;
        for (const auto& [_, frame] : frames_)
            page_size = std::max(page_size, std::max(frame.image.width(), frame.image.height()) + 2 * padding);
    }
    check(page_size > 0 && page_size <= image_resource::max_dimension, "Invalid page size.");
    check(padding >= 1 && padding <= page_size / 2, "Invalid page padding.");
    std::vector<std::pair<std::string, const sprite_frame*>> pending;
    for (const auto& [name, frame] : frames_) {
        check(frame.image.width() <= page_size - 2 * padding && frame.image.height() <= page_size - 2 * padding,
            "Frame exceeds sprite page size including padding.");
        pending.emplace_back(name, &frame);
    }
    packed_artwork output;
    while (!pending.empty()) {
        std::vector<stbrp_node> nodes(page_size);
        stbrp_context context{}; stbrp_init_target(&context, page_size, page_size, nodes.data(), int(nodes.size()));
        std::vector<stbrp_rect> rects;
        for (std::size_t i = 0; i < pending.size(); ++i) {
            const auto& img = pending[i].second->image;
            rects.push_back({int(i), img.width() + 2 * padding, img.height() + 2 * padding});
        }
        stbrp_pack_rects(&context, rects.data(), int(rects.size()));
        int w = 0, h = 0;
        for (const auto& r : rects) if (r.was_packed) { w = std::max(w, r.x + r.w); h = std::max(h, r.y + r.h); }
        check(w > 0 && h > 0, "Unable to pack frames.");
        image_buffer pixels(std::size_t(w) * h * 4, 0);
        std::vector<std::pair<std::string, const sprite_frame*>> remaining;
        for (const auto& r : rects) {
            const auto& [name, frame] = pending.at(r.id);
            if (!r.was_packed) { remaining.push_back(pending.at(r.id)); continue; }
            const auto& img = frame->image;
            for (int y = 0; y < r.h; ++y) {
                const auto row = img.row(std::clamp(y - padding, 0, img.height() - 1));
                for (int x = 0; x < r.w; ++x) {
                    auto source = row.data() + 4 * std::clamp(x - padding, 0, img.width() - 1);
                    std::copy_n(source, 4, pixels.data() + (std::size_t(r.y + y) * w + r.x + x) * 4);
                }
            }
            output.frames.push_back({name, output.pages.size(), {r.x + padding, r.y + padding, img.width(), img.height()}, frame->registration_origin});
        }
        output.pages.push_back({image_resource::from_rgba(w, h, std::move(pixels)).encode_png(), w, h});
        pending = std::move(remaining);
    }
    return output;
}
