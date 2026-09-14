#pragma once
#include "sm_image.hpp"
#include "sm_object_id.hpp"
#include "sm_types.hpp"
#include <map>
#include <optional>
#include <string>
#include <unordered_map>

namespace sm {
    struct sprite_frame { image_resource image; point registration_origin{}; };
    enum class bone_anchor { root, tip };
    struct slot_definition {
        object_id bone;
        bone_anchor anchor = bone_anchor::root;
        std::vector<std::string> states{"default"};
    };
    struct sprite_transform {
        point translation{};
        double rotation = 0;
        point scale{1, 1};
    };
    using frame_target = std::optional<std::string>;
    struct appearance_slot {
        std::string slot;
        std::map<std::string, frame_target> states{{"default", std::nullopt}};
        sprite_transform transform;
    };
    struct appearance { std::vector<appearance_slot> appearance_slots; };
    struct packed_frame_region {
        std::string name;
        std::size_t page;
        pixel_rect rect;
        point registration_origin;
    };
    struct packed_sprite_page { image_buffer png; int width, height; };
    struct packed_artwork {
        std::vector<packed_sprite_page> pages;
        std::vector<packed_frame_region> frames;
    };

    // Access is read-only; semantic operations throw invalid_argument for invalid
    // edits and out_of_range for missing lookup targets. Copies have independent
    // semantics and share immutable pixels.
    class artwork {
        std::map<std::string, sprite_frame> frames_;
        std::map<std::string, slot_definition> definitions_;
        std::map<std::string, appearance> appearances_;
        void validate_appearance(const appearance& value) const;
    public:
        static constexpr int max_frame_dimension = image_resource::max_dimension - 2;
        const auto& frames() const noexcept { return frames_; }
        const auto& slot_definitions() const noexcept { return definitions_; }
        const auto& appearances() const noexcept { return appearances_; }
        void insert_frame(const std::string& name, std::span<const std::uint8_t> encoded);
        void insert_frame(const std::string& name, sprite_frame frame);
        void rename_frame(const std::string& name, const std::string& replacement);
        void delete_frame(const std::string& name); // Rejects referenced frames.
        void set_registration_origin(const std::string& name, point origin);
        void add_slot(const std::string& name, slot_definition definition);
        void rename_slot(const std::string& name, const std::string& replacement);
        void delete_slot(const std::string& name);
        void bind_slot(const std::string& name, object_id bone, bone_anchor anchor);
        void add_state(const std::string& slot, const std::string& state);
        void rename_state(const std::string& slot, const std::string& state, const std::string& replacement);
        void delete_state(const std::string& slot, const std::string& state);
        void add_appearance(const std::string& name, appearance value = {});
        void rename_appearance(const std::string& name, const std::string& replacement);
        void delete_appearance(const std::string& name);
        void set_appearance(const std::string& name, appearance value);
        frame_target resolve_frame(const std::string& appearance, const std::string& slot,
            const std::string& state = "default") const;
        void remap_bones(const std::unordered_map<object_id, object_id>& remap);
        // Generated layout is not stable identity. Rectangles exclude extruded padding.
        // Zero chooses at least 2048, growing to fit the largest padded frame.
        packed_artwork pack(int page_size = 0, int padding = 1) const;
    };
}
