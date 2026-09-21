#pragma once

#include "tool.hpp"
#include "rig_interaction.hpp"
#include <optional>

namespace ui::tool {

    class animate : public base {
    public:
        using authored_action = rig_interaction::authored_action;
        using animation_authoring = rig_interaction::animation_authoring;

        animate();

        void init(canvas::manager& canvases, mdl::project& model) override;
        void activate(canvas::manager& canvases) override;
        void deactivate(canvas::manager& canvases) override;

        void keyPressEvent(canvas::scene& c, QKeyEvent* event) override;
        void keyReleaseEvent(canvas::scene& c, QKeyEvent* event) override;
        void mousePressEvent(canvas::scene& c, QGraphicsSceneMouseEvent* event) override;
        void mouseMoveEvent(canvas::scene& c, QGraphicsSceneMouseEvent* event) override;
        void mouseReleaseEvent(canvas::scene& c, QGraphicsSceneMouseEvent* event) override;

        QWidget* settings_widget() override;
        void set_animation_authoring(std::optional<animation_authoring> authoring);

    private:
        rig_interaction interaction_;
    };
}
