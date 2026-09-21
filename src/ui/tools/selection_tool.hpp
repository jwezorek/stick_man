#pragma once

#include "tool.hpp"
#include "rig_interaction.hpp"

namespace ui::tool {

    class select : public base {
    public:
        select();

        void init(canvas::manager& canvases, mdl::project& model) override;
        void activate(canvas::manager& canvases) override;
        void deactivate(canvas::manager& canvases) override;

        void keyPressEvent(canvas::scene& c, QKeyEvent* event) override;
        void keyReleaseEvent(canvas::scene& c, QKeyEvent* event) override;
        void mousePressEvent(canvas::scene& c, QGraphicsSceneMouseEvent* event) override;
        void mouseMoveEvent(canvas::scene& c, QGraphicsSceneMouseEvent* event) override;
        void mouseReleaseEvent(canvas::scene& c, QGraphicsSceneMouseEvent* event) override;

        QWidget* settings_widget() override;

    private:
        rig_interaction interaction_;
    };
}
