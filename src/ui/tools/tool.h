#pragma once

#include "../canvas/scene.h"
#include <QWidget>
#include <QtWidgets>
#include <vector>
#include <memory>
#include <span>
#include <optional>
#include <functional>

/*------------------------------------------------------------------------------------------------*/

namespace mdl {
    class project;
}

namespace ui {

    namespace pane {
        class tool_settings;
    }

    namespace canvas {
        class manager;
    }

    namespace tool {

        class input_handler {
        public:
            virtual void keyPressEvent(canvas::scene& c, QKeyEvent* event) = 0;
            virtual void keyReleaseEvent(canvas::scene& c, QKeyEvent* event) = 0;
            virtual void mousePressEvent(canvas::scene& c, QGraphicsSceneMouseEvent* event) = 0;
            virtual void mouseMoveEvent(canvas::scene& c, QGraphicsSceneMouseEvent* event) = 0;
            virtual void mouseReleaseEvent(canvas::scene& c, QGraphicsSceneMouseEvent* event) = 0;
            virtual void mouseDoubleClickEvent(canvas::scene& c, QGraphicsSceneMouseEvent* event) = 0;
            virtual void wheelEvent(canvas::scene& c, QGraphicsSceneWheelEvent* event) = 0;
        };

        enum class id {
            none,
            selection,
            animate,
            pan,
            zoom,
            add_node,
            add_bone
        };

        struct fields {
            id id;
            QString name;
            QString rsrc_name;
        };

        class manager;

        class base : public input_handler {
        public:
            base(QString name, QString rsrc, id id);
            tool::id id() const;
            QString name() const;
            QString icon_rsrc() const;
            void populate_settings(pane::tool_settings* pane);
            virtual void activate(canvas::manager& canvases);
            virtual void keyPressEvent(canvas::scene& c, QKeyEvent* event) override;
            virtual void keyReleaseEvent(canvas::scene& c, QKeyEvent* event) override;
            virtual void mousePressEvent(canvas::scene& c, QGraphicsSceneMouseEvent* event) override;
            virtual void mouseMoveEvent(canvas::scene& c, QGraphicsSceneMouseEvent* event) override;
            virtual void mouseReleaseEvent(canvas::scene& c, QGraphicsSceneMouseEvent* event) override;
            virtual void mouseDoubleClickEvent(canvas::scene& c, QGraphicsSceneMouseEvent* event) override;
            virtual void wheelEvent(canvas::scene& c, QGraphicsSceneWheelEvent* event) override;
            virtual void deactivate(canvas::manager& canvases);
            virtual void init(canvas::manager& canvases, mdl::project& model);
            virtual QWidget* settings_widget();
            virtual ~base();

        private:
            tool::id id_;
            QString name_;
            QString rsrc_;
        };
    }
}