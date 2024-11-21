#pragma once

#include "../canvas/scene.h"
#include <QWidget>
#include <QtWidgets>
#include "../../core/sm_types.h"
#include "properties.h"
#include <functional>

/*------------------------------------------------------------------------------------------------*/

namespace ui {

    class manager;
    class stick_man;
    namespace canvas {
        class manager;
    }

    namespace pane {

        class skeleton;

        class main_skeleton : public QSplitter {
            friend class skeleton;

            skeleton* parent_;
            canvas::manager* canvases_;
            mdl::project* project_;
            QTreeView* skeleton_tree_;
            selection_properties* sel_properties_;

            QMetaObject::Connection tree_sel_conn_;
            QMetaObject::Connection tree_conn_;
            QMetaObject::Connection canv_sel_conn_;
            QMetaObject::Connection canv_content_conn_;

            void expand_selected_items();

            void connect_tree_sel_handler();
            void disconnect_tree_sel_handler();

            void connect_canv_sel_handler();
            void disconnect_canv_sel_handler();

            void connect_canv_cont_handler();
            void disconnect_canv_cont_handler();

            void connect_tree_change_handler();
            void disconnect_tree_change_handler();

            void sync_with_model(sm::world& model);
            void handle_canv_sel_change();
            void handle_tree_change(QStandardItem* item);
            void handle_tree_selection_change(const QItemSelection&, const QItemSelection&);
            void handle_rename(mdl::skel_piece piece, const std::string& new_name);
            void traverse_tree_items(const std::function<void(QStandardItem*)>& visitor);

            QTreeView* create_skeleton_tree();
            std::vector<QStandardItem*> selected_items() const;
            canvas::scene& canvas();

            void select_item(QStandardItem* item, bool select);
            void select_items(const std::vector<QStandardItem*>& items, bool emit_signal = true);

        public:

            main_skeleton(skeleton* parent, ui::stick_man* mgr);
            selection_properties& sel_properties();
            void init(canvas::manager& canvases, mdl::project& proj);
            bool validate_props_name_change(const std::string& new_name);
        };

    }

}