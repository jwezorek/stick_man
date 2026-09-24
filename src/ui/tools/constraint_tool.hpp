#pragma once

#include "tool.hpp"
#include "../canvas/constraint_adornment.hpp"
#include <optional>

namespace ui::tool {

class constraint : public base {
    enum class operation { select, rotation, rigid_triangle };

    struct drag_state {
        sm::object_id id;
        canvas::constraint_part part;
        sm::constraint_definition original;
    };

    struct triangle_sweep_state {
        canvas::scene* scene = nullptr;
        QPointF last_point;
        std::optional<sm::object_id> first_bone;
        std::optional<sm::object_id> last_crossed_bone;
        QGraphicsPathItem* trail = nullptr;
        QGraphicsLineItem* first_highlight = nullptr;
    };

    QWidget* settings_ = nullptr;
    QComboBox* operation_ = nullptr;
    QComboBox* reference_ = nullptr;
    QLabel* reference_label_ = nullptr;
    mdl::project* model_ = nullptr;
    canvas::manager* canvases_ = nullptr;
    std::optional<sm::object_id> pending_bone_;
    std::optional<drag_state> drag_;
    std::optional<triangle_sweep_state> triangle_sweep_;
    bool press_handled_ = false;
    QGraphicsLineItem* pending_highlight_ = nullptr;
    canvas::scene* pending_scene_ = nullptr;

    operation current_operation() const;
    sm::rotation_reference_kind current_reference_kind() const;
    void update_settings_state();
    void clear_pending();
    void show_pending(canvas::scene& canv, const sm::bone& bone, const QString& message);
    void clear_triangle_sweep(bool hide_status = true);
    void begin_triangle_sweep(canvas::scene& canv, QPointF point);
    void update_triangle_sweep(canvas::scene& canv, QPointF point);
    void set_triangle_sweep_first(canvas::scene& canv, const sm::bone& bone);
    void process_triangle_sweep_bone(canvas::scene& canv, sm::bone& bone);
    void cancel_drag(canvas::scene& canv);
    void update_drag(canvas::scene& canv, QPointF point);
    void finish_drag(canvas::scene& canv);
    void create_rotation(canvas::scene& canv, sm::bone& target);
    void create_triangle(canvas::scene& canv, sm::bone& bone);
    std::optional<sm::object_id> add_triangle(canvas::scene& canv, sm::object_id first, sm::object_id second);
    void report_failure(canvas::scene& canv, sm::result result, const QString& action);

public:
    constraint();
    void init(canvas::manager& canvases, mdl::project& model) override;
    void activate(canvas::manager& canvases) override;
    void deactivate(canvas::manager& canvases) override;
    void keyPressEvent(canvas::scene& c, QKeyEvent* event) override;
    void mousePressEvent(canvas::scene& c, QGraphicsSceneMouseEvent* event) override;
    void mouseMoveEvent(canvas::scene& c, QGraphicsSceneMouseEvent* event) override;
    void mouseReleaseEvent(canvas::scene& c, QGraphicsSceneMouseEvent* event) override;
    QWidget* settings_widget() override;
};

} // namespace ui::tool
