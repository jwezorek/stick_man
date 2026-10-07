#pragma once

#include <QDockWidget>
#include "properties.hpp"

namespace ui {
    class stick_man;
    namespace canvas { class manager; }
    namespace pane {
        // Dock/visibility ownership is independent of the Skeleton tree.
        class selection_properties_pane : public QDockWidget {
            selection_properties* properties_;
            canvas::manager* canvases_ = nullptr;
        public:
            explicit selection_properties_pane(ui::stick_man* window);
            void init(canvas::manager& canvases, mdl::project& project);
            void set_animation_mode(bool active);
            selection_properties& properties() const { return *properties_; }
        };
    }
}
