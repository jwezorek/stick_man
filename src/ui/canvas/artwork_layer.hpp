#pragma once
#include "../../model/project.hpp"
#include <QtWidgets>

namespace ui::canvas {
    class scene;
    inline constexpr auto frame_mime_type = "application/x-stickman-frame";
    struct sprite_selection {
        sm::object_id character;
        std::string appearance;
        std::string slot;
        bool operator==(const sprite_selection&) const = default;
    };
    enum class sprite_drag { translate, rotate, scale_x, scale_y, scale_xy };
    enum class skeleton_display { hidden, wireframe_nodes, wireframe, visible };
    class artwork_layer : public QObject {
        Q_OBJECT
        scene& scene_;
        mdl::project& project_;
        const sm::topology* preview_topology_ = nullptr;
        std::unordered_map<sm::object_id, std::string> active_;
        std::unordered_map<sm::object_id, std::map<std::string, std::string>> preview_states_;
        std::optional<sprite_selection> selected_;
        bool transform_editing_ = false;
        bool show_artwork_ = true;
        skeleton_display skeleton_display_ = skeleton_display::visible;
        struct drag_state {
            sprite_selection selection;
            sm::sprite_transform before, preview;
            sm::matrix bone_inverse;
            sm::point start;
            sprite_drag mode;
        };
        std::optional<drag_state> drag_;
        struct drawable {
            sprite_selection selection;
            sm::image_resource image;
            sm::matrix transform, bone_transform;
        };
        struct transform_target {
            sprite_selection selection;
            sprite_drag mode;
        };
        std::vector<drawable> drawables() const;
        std::optional<transform_target> transform_target_at(QPointF position) const;
        bool begin_transform(QPointF position, const transform_target& target);
    public:
        artwork_layer(scene& scene, mdl::project& project);
        void set_preview_topology(const sm::topology* topology) { cancel_transform(); preview_topology_ = topology; }
        std::string active_appearance(const sm::object_id& character) const;
        void set_active_appearance(const sm::object_id& character, const std::string& appearance);
        std::string preview_state(const sm::object_id& character, const std::string& slot) const;
        void set_preview_state(const sm::object_id& character, const std::string& slot, const std::string& state);
        void reset_preview_states(const sm::object_id& character);
        void set_selected_slot(const sm::object_id& character, const std::string& slot);
        void clear_selected_slot();
        const std::optional<sprite_selection>& selected_slot() const { return selected_; }
        std::optional<sm::sprite_transform> selected_transform() const;
        std::optional<sprite_selection> hit_test(QPointF position) const;
        void paint(QPainter& painter) const;
        void paint_selection(QPainter& painter) const;
        void refresh();
        void reset();
        void set_show_artwork(bool show);
        void set_skeleton_display(skeleton_display display);
        bool show_artwork() const { return show_artwork_; }
        bool show_skeleton() const { return skeleton_display_ != skeleton_display::hidden; }
        skeleton_display skeleton_display_mode() const { return skeleton_display_; }
        void refresh_guides();
        void set_transform_editing(bool enabled);
        bool transform_editing() const { return transform_editing_; }
        bool begin_transform(QPointF position);
        bool begin_transform(QPointF position, sprite_drag mode);
        void update_transform(QPointF position);
        void end_transform(QPointF position);
        void cancel_transform();
        bool dragging() const { return drag_.has_value(); }
        // Empty slot creates a uniquely named root-anchored channel. One undo step.
        void assign_frame(const sm::object_id& character, const sm::object_id& bone,
            const std::string& frame, const std::optional<std::string>& slot = {});
        bool can_drop(const QMimeData* mime, QPointF position) const;
        bool drop_frame(const QMimeData* mime, QPointF position);
    signals:
        void selection_changed();
        void appearance_changed();
        void preview_changed();
        void transform_changed();
    };
}
