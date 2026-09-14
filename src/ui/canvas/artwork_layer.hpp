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
    class artwork_layer : public QObject {
        Q_OBJECT
        scene& scene_;
        mdl::project& project_;
        std::unordered_map<sm::object_id, std::string> active_;
        std::optional<sprite_selection> selected_;
        bool show_artwork_ = true, show_skeleton_ = true, wireframe_ = false;
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
        std::vector<drawable> drawables() const;
        std::optional<sm::sprite_transform> selected_transform() const;
    public:
        artwork_layer(scene& scene, mdl::project& project);
        std::string active_appearance(const sm::object_id& character) const;
        void set_active_appearance(const sm::object_id& character, const std::string& appearance);
        void set_selected_slot(const sm::object_id& character, const std::string& slot);
        const std::optional<sprite_selection>& selected_slot() const { return selected_; }
        std::optional<sprite_selection> hit_test(QPointF position) const;
        void paint(QPainter& painter) const;
        void paint_selection(QPainter& painter) const;
        void refresh();
        void reset();
        void set_show_artwork(bool show);
        void set_show_skeleton(bool show);
        void set_wireframe(bool wireframe);
        bool show_artwork() const { return show_artwork_; }
        bool show_skeleton() const { return show_skeleton_; }
        bool wireframe() const { return wireframe_; }
        void refresh_guides();
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
    };
}
