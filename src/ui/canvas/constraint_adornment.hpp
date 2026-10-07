#pragma once

#include "../../core/sm_skeleton.hpp"
#include <QGraphicsItem>
#include <QPointF>
#include <map>
#include <optional>
#include <vector>

namespace ui::canvas {

class scene;

enum class constraint_part {
    body,
    rotation_min,
    rotation_max,
    triangle_angle
};

struct constraint_hit {
    sm::object_id id;
    constraint_part part = constraint_part::body;
    bool transition_local = false;
};

class constraint_adornment_layer {
public:
    explicit constraint_adornment_layer(scene& owner);
    ~constraint_adornment_layer();

    void sync(const sm::topology& topology, const sm::constraint_map& constraints,
        const sm::constraint_map& transition_constraints,
        const sm::constraint_map& incoming_locked_constraints, double scale);
    void clear();
    void set_visible(bool visible);
    bool visible() const noexcept { return visible_; }
    void set_handles_visible(bool visible);

    void set_selected(std::optional<sm::object_id> id);
    void set_hovered(std::optional<sm::object_id> id);
    std::optional<constraint_hit> hit(const QPointF& point) const;

private:
    struct visual {
        std::vector<QGraphicsItem*> graphics;
        bool transition_local = false;
    };

    scene& owner_;
    std::map<sm::object_id, visual> visuals_;
    bool visible_ = false;
    bool handles_visible_ = false;
    std::optional<sm::object_id> selected_;
    std::optional<sm::object_id> hovered_;

    void update_styles();
};

} // namespace ui::canvas
