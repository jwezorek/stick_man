#pragma once

#include <QWidget>
#include <QtWidgets>
#include "tree_view.h"
#include "../../model/project.h"

/*------------------------------------------------------------------------------------------------*/

namespace ui {

    namespace canvas {
        class manager;
    }

    namespace pane {

        class skeleton;

        class abstract_skeleton_pane : public QWidget {
        protected:
            skeleton* parent_;
            canvas::manager* canvases_;
            mdl::project* project_;
            QLayout* layout_;

            QMetaObject::Connection tree_sel_conn_;
            QMetaObject::Connection tree_conn_;
            QMetaObject::Connection canv_sel_conn_;
            QMetaObject::Connection canv_content_conn_;

            void connect_tree_sel_handler();
            void disconnect_tree_sel_handler();

            void connect_canv_sel_handler();
            void disconnect_canv_sel_handler();

            void connect_canv_cont_handler();
            void disconnect_canv_cont_handler();

            void connect_tree_change_handler();
            void disconnect_tree_change_handler();

            tree_view& skel_tree();
            void populate();

            virtual const tree_view& skel_tree() const = 0;
            virtual QWidget* create_content(skeleton* parent) = 0;
            virtual void handle_canv_sel_change() = 0;
            virtual void handle_tree_change(QStandardItem* item) = 0;
            virtual void handle_tree_selection_change(
                const QItemSelection&, const QItemSelection&) = 0;
            virtual void sync_with_model(sm::world& model) = 0;
            virtual void init_aux(canvas::manager& canvases, mdl::project& proj) = 0;

        public:

            abstract_skeleton_pane(skeleton* parent);
            void init(canvas::manager& canvases, mdl::project& proj);

        };

    }
}