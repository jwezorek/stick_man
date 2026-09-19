#pragma once

#include "tool.hpp"

namespace ui {

    namespace tool {

        class constraint : public base {
        private:
            QWidget* settings_;
            QCheckBox* absolute_constraint_;
            mdl::project* model_;

        public:
            constraint();
            void init(canvas::manager& canvases, mdl::project& model) override;
            void activate(canvas::manager& canvases) override;
            void deactivate(canvas::manager& canvases) override;
            void mouseReleaseEvent(canvas::scene& c, QGraphicsSceneMouseEvent* event) override;
            QWidget* settings_widget() override;
        };

    }
}
